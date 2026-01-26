#include "rewind/core/analysis/trace_control_flow_analyzer.hpp"

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "rewind/core/decode/bn_block_decoder.hpp"
#include "rewind/core/decode/trace_mode.hpp"
#include "w1rewind/format/trace_format.hpp"
#include "w1rewind/replay/flow_cursor.hpp"
#include "w1rewind/trace/trace_reader.hpp"

namespace binja::rewind::core::analysis {

namespace {

using decode::InstructionDecoder;

struct edge_key {
  uint64_t from = 0;
  uint64_t to = 0;
  uint8_t mode_bits = 0;

  bool operator==(const edge_key& other) const {
    return from == other.from && to == other.to && mode_bits == other.mode_bits;
  }
};

struct edge_key_hash {
  size_t operator()(const edge_key& key) const {
    size_t seed = std::hash<uint64_t>{}(key.from);
    seed ^= std::hash<uint64_t>{}(key.to) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= std::hash<uint8_t>{}(key.mode_bits) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct instruction_key {
  uint64_t address = 0;
  uint8_t mode_bits = 0;

  bool operator==(const instruction_key& other) const {
    return address == other.address && mode_bits == other.mode_bits;
  }
};

struct instruction_key_hash {
  size_t operator()(const instruction_key& key) const {
    size_t seed = std::hash<uint64_t>{}(key.address);
    seed ^= std::hash<uint8_t>{}(key.mode_bits) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct xref_cache_key {
  BNFunction* func = nullptr;
  BNArchitecture* arch = nullptr;
  uint64_t from = 0;

  bool operator==(const xref_cache_key& other) const {
    return func == other.func && arch == other.arch && from == other.from;
  }
};

struct xref_cache_hash {
  size_t operator()(const xref_cache_key& key) const {
    size_t seed = std::hash<void*>{}(key.func);
    seed ^= std::hash<void*>{}(key.arch) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= std::hash<uint64_t>{}(key.from) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct branch_info {
  bool ok = false;
  uint64_t trace_address = 0;
  uint32_t length = 0;
  branch_summary summary{};
  InstructionDecoder::instruction_mode mode{};
};

uint8_t mode_bits(const InstructionDecoder::instruction_mode& mode) {
  uint8_t bits = 0;
  if (mode.mode_valid) {
    bits |= 0x1;
  }
  if (mode.thumb) {
    bits |= 0x2;
  }
  return bits;
}

InstructionDecoder::instruction_mode mode_from_bits(uint8_t bits) {
  InstructionDecoder::instruction_mode mode{};
  mode.mode_valid = (bits & 0x1) != 0;
  mode.thumb = (bits & 0x2) != 0;
  return mode;
}

InstructionDecoder::instruction_mode mode_from_step(
    const w1::rewind::replay_context& context, const w1::rewind::flow_step& step
) {
  return decode::instruction_mode_from_step(context, step);
}

branch_summary summarize_branches(const InstructionDecoder::instruction_detail& detail) {
  branch_summary summary{};
  summary.is_call = detail.semantics.is_call;
  summary.is_return = detail.semantics.is_return;
  summary.is_syscall = detail.semantics.is_syscall;

  bool has_branch = false;
  bool has_direct = false;
  bool has_indirect = false;
  for (size_t i = 0; i < detail.info.branchCount; ++i) {
    switch (detail.info.branchType[i]) {
    case UnconditionalBranch:
    case TrueBranch:
    case FalseBranch:
      has_branch = true;
      has_direct = true;
      break;
    case IndirectBranch:
    case UnresolvedBranch:
      has_branch = true;
      has_indirect = true;
      break;
    case FunctionReturn:
      summary.is_return = true;
      break;
    case SystemCall:
      summary.is_syscall = true;
      break;
    case CallDestination:
      summary.is_call = true;
      if (detail.info.branchTarget[i] != 0) {
        has_direct = true;
      } else {
        has_indirect = true;
      }
      break;
    default:
      break;
    }
  }

  summary.is_branch = has_branch;
  summary.is_direct = has_direct;
  summary.is_indirect = has_indirect;
  if (summary.is_syscall) {
    summary.is_call = false;
  }
  if (!summary.is_direct && !summary.is_indirect && summary.is_call) {
    summary.is_indirect = true;
  }
  return summary;
}

bool fill_branch_info(
    const InstructionDecoder& decoder, uint64_t trace_address, InstructionDecoder::instruction_mode mode,
    branch_info& out, std::string& error
) {
  InstructionDecoder::instruction_detail detail{};
  if (!decoder.decode_instruction_detail(trace_address, detail, error, mode)) {
    return false;
  }
  out.ok = true;
  out.trace_address = trace_address;
  out.length = static_cast<uint32_t>(detail.semantics.length);
  out.summary = summarize_branches(detail);
  out.mode = mode;
  return true;
}

} // namespace

ControlFlowEdgeResult TraceControlFlowAnalyzer::add_control_flow_edges(
    const w1::rewind::replay_session& session, const std::shared_ptr<w1::rewind::trace_index>& index,
    const mapping::AddressMapper& mapper, const BinaryNinja::Ref<BinaryNinja::BinaryView>& view,
    const std::string& trace_path, const BinaryNinja::Ref<BinaryNinja::Logger>& logger,
    const std::function<void(const std::string&)>& progress, const std::function<bool()>& is_cancelled,
    const control_flow_edge_options& options
) const {
  ControlFlowEdgeResult result{};

  if (!index) {
    result.error = "trace index unavailable";
    return result;
  }
  if (!view) {
    result.error = "binary view unavailable";
    return result;
  }

  auto stream = std::make_shared<w1::rewind::trace_reader>(trace_path);
  w1::rewind::record_stream_cursor stream_cursor(stream);
  w1::rewind::flow_extractor extractor(&session.context());
  w1::rewind::history_window history(1);
  w1::rewind::flow_cursor cursor(std::move(stream_cursor), std::move(extractor), std::move(history), index);
  if (!cursor.open()) {
    result.error = std::string(cursor.error());
    return result;
  }

  const auto& threads = session.threads();
  if (threads.empty()) {
    result.error = "trace has no threads";
    return result;
  }

  decode::BnBlockDecoder block_decoder(view, &mapper);
  InstructionDecoder instruction_decoder(view, &mapper);

  std::unordered_map<instruction_key, branch_info, instruction_key_hash> instruction_cache;
  std::unordered_map<uint64_t, branch_info> block_cache;
  std::unordered_map<edge_key, branch_summary, edge_key_hash> edges;

  if (progress) {
    progress("Scanning trace for control flow edges...");
  }

  cursor.set_cancel_checker(is_cancelled);

  for (const auto& thread : threads) {
    if (is_cancelled && is_cancelled()) {
      result.error = "cancelled";
      return result;
    }
    if (!cursor.seek(thread.thread_id, 0)) {
      if (logger) {
        std::string cursor_error(cursor.error());
        logger->LogWarn(
            "Rewind: flow edge scan failed to seek thread %llu: %s", static_cast<unsigned long long>(thread.thread_id),
            cursor_error.c_str()
        );
      }
      continue;
    }

    std::optional<w1::rewind::flow_step> prev_step;
    std::vector<uint64_t> call_stack;
    w1::rewind::flow_step step{};
    while (cursor.step_forward(step)) {
      ++result.steps_scanned;
      if (!prev_step.has_value()) {
        prev_step = step;
        continue;
      }

      ++result.transitions;
      const auto& from_step = *prev_step;

      branch_info info{};
      bool decoded = false;
      if (from_step.is_block) {
        auto it = block_cache.find(from_step.block_id);
        if (it != block_cache.end()) {
          info = it->second;
          decoded = info.ok;
        } else {
          w1::rewind::decoded_block decoded_block{};
          std::string decode_error;
          if (block_decoder.decode_block(session.context(), from_step, decoded_block, decode_error) &&
              !decoded_block.instructions.empty()) {
            ++result.blocks_decoded;
            auto& last_inst = decoded_block.instructions.back();
            InstructionDecoder::instruction_mode mode = mode_from_step(session.context(), from_step);
            if (fill_branch_info(instruction_decoder, last_inst.address, mode, info, decode_error)) {
              info.length = last_inst.size ? last_inst.size : info.length;
              decoded = true;
            }
          }
          if (!decoded) {
            info.ok = false;
            info.trace_address = from_step.address;
            info.mode = mode_from_step(session.context(), from_step);
          }
          block_cache.emplace(from_step.block_id, info);
        }
      } else {
        InstructionDecoder::instruction_mode mode = mode_from_step(session.context(), from_step);
        instruction_key key{from_step.address, mode_bits(mode)};
        auto it = instruction_cache.find(key);
        if (it != instruction_cache.end()) {
          info = it->second;
          decoded = info.ok;
        } else {
          std::string decode_error;
          if (fill_branch_info(instruction_decoder, from_step.address, mode, info, decode_error)) {
            decoded = true;
          } else {
            info.ok = false;
            info.trace_address = from_step.address;
            info.length = from_step.size;
            info.mode = mode;
          }
          instruction_cache.emplace(key, info);
        }
      }

      if (!decoded) {
        ++result.edges_skipped_decode;
        prev_step = step;
        continue;
      }

      bool prev_is_call = info.summary.is_call && !info.summary.is_return && !info.summary.is_syscall;
      bool prev_is_return = info.summary.is_return;
      if (prev_is_call && info.length != 0) {
        call_stack.push_back(info.trace_address + info.length);
      }
      if (prev_is_return) {
        if (!call_stack.empty()) {
          call_stack.pop_back();
        }
      } else if ((info.summary.is_call || info.summary.is_branch) && !call_stack.empty() &&
                 step.address == call_stack.back()) {
        ++result.edges_skipped_return_gap;
        call_stack.pop_back();
        prev_step = step;
        continue;
      }

      if (!should_emit_edge(info.summary, options)) {
        ++result.edges_skipped_no_branch;
        prev_step = step;
        continue;
      }

      const uint64_t from_size = info.length ? info.length : 1;
      const uint64_t to_size = step.size ? step.size : 1;
      auto from_view = mapper.trace_to_view(info.trace_address, static_cast<size_t>(from_size));
      auto to_view = mapper.trace_to_view(step.address, static_cast<size_t>(to_size));
      if (!from_view.has_value() || !to_view.has_value()) {
        ++result.edges_skipped_no_mapping;
        prev_step = step;
        continue;
      }

      edge_key key{*from_view, *to_view, mode_bits(info.mode)};
      auto [it, inserted] = edges.emplace(key, info.summary);
      if (inserted) {
        ++result.edges_found;
      } else {
        it->second.is_call = it->second.is_call || info.summary.is_call;
        it->second.is_branch = it->second.is_branch || info.summary.is_branch;
        it->second.is_direct = it->second.is_direct || info.summary.is_direct;
        it->second.is_indirect = it->second.is_indirect || info.summary.is_indirect;
        it->second.is_return = it->second.is_return || info.summary.is_return;
        it->second.is_syscall = it->second.is_syscall || info.summary.is_syscall;
      }

      prev_step = step;
    }

    auto kind = cursor.error_kind();
    if (kind == w1::rewind::flow_error_kind::other && cursor.error() == "cancelled") {
      result.error = "cancelled";
      return result;
    }
    if (kind != w1::rewind::flow_error_kind::end_of_trace) {
      if (logger) {
        std::string cursor_error(cursor.error());
        logger->LogWarn(
            "Rewind: flow edge scan halted on thread %llu: %s", static_cast<unsigned long long>(thread.thread_id),
            cursor_error.c_str()
        );
      }
    }
  }

  if (edges.empty()) {
    result.ok = true;
    return result;
  }

  BinaryNinja::ExecuteOnMainThreadAndWait([&]() {
    std::unordered_map<xref_cache_key, std::unordered_set<uint64_t>, xref_cache_hash> existing_refs;
    auto undo = view->BeginUndoActions();

    for (const auto& [edge, summary] : edges) {
      if (!view->GetSegmentAt(edge.from) || !view->GetSegmentAt(edge.to)) {
        ++result.edges_skipped_no_segment;
        continue;
      }

      auto funcs = view->GetAnalysisFunctionsContainingAddress(edge.from);
      if (funcs.empty()) {
        ++result.edges_skipped_no_function;
        continue;
      }

      InstructionDecoder::instruction_mode mode = mode_from_bits(edge.mode_bits);
      for (auto& func : funcs) {
        if (!func) {
          ++result.edges_skipped_no_function;
          continue;
        }
        auto arch = func->GetArchitecture();
        if (!arch) {
          ++result.edges_skipped_no_arch;
          continue;
        }

        uint64_t decode_addr = edge.from;
        if (mode.mode_valid) {
          if (mode.thumb) {
            decode_addr |= 1ULL;
          } else {
            decode_addr &= ~1ULL;
          }
        }
        if (auto associated = arch->GetAssociatedArchitectureByAddress(decode_addr); associated) {
          arch = associated;
        }

        xref_cache_key cache_key{func->GetObject(), arch->GetObject(), edge.from};
        auto it = existing_refs.find(cache_key);
        if (it == existing_refs.end()) {
          BinaryNinja::ReferenceSource src{};
          src.func = func;
          src.arch = arch;
          src.addr = edge.from;
          auto refs = view->GetCodeReferencesFrom(src);
          std::unordered_set<uint64_t> targets(refs.begin(), refs.end());
          it = existing_refs.emplace(cache_key, std::move(targets)).first;
        }

        if (it->second.find(edge.to) != it->second.end()) {
          ++result.edges_skipped_existing;
          continue;
        }

        func->AddUserCodeReference(arch, edge.from, edge.to);
        it->second.insert(edge.to);
        ++result.edges_added;
        if (logger) {
          const char* kind = summary.is_call ? "call" : (summary.is_branch ? "branch" : "flow");
          const char* dir = summary.is_indirect ? "indirect" : (summary.is_direct ? "direct" : "unknown");
          logger->LogDebug(
              "Rewind: add xref %s %s from=0x%llx to=0x%llx func=0x%llx", dir, kind,
              static_cast<unsigned long long>(edge.from), static_cast<unsigned long long>(edge.to),
              static_cast<unsigned long long>(func->GetStart())
          );
        }
      }
    }

    view->ForgetUndoActions(undo);
  });

  result.ok = result.error.empty();
  return result;
}

} // namespace binja::rewind::core::analysis
