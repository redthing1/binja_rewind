#include "doctest/doctest.hpp"

#include "rewind/core/engine/breakpoint_matcher.hpp"
#include "w1rewind/replay/block_decoder.hpp"
#include "w1rewind/replay/replay_context.hpp"

namespace {

class test_decoder final : public w1::rewind::block_decoder {
public:
  bool decode_block(
      const w1::rewind::replay_context&, const w1::rewind::flow_step& flow, w1::rewind::decoded_block& out,
      std::string& error
  ) override {
    if (flow.size == 0) {
      error = "block size is zero";
      return false;
    }
    out.start = flow.address;
    out.size = flow.size;
    out.instructions.clear();
    w1::rewind::decoded_instruction first{};
    first.address = flow.address;
    first.size = 2;
    w1::rewind::decoded_instruction second{};
    second.address = flow.address + 2;
    second.size = 2;
    out.instructions.push_back(first);
    out.instructions.push_back(second);
    return true;
  }
};

w1::rewind::replay_context make_context() {
  w1::rewind::replay_context ctx;
  ctx.header.flags = w1::rewind::trace_flag_blocks;
  w1::rewind::block_definition_record def{};
  def.block_id = 1;
  def.address = 0x1000;
  def.size = 4;
  ctx.blocks_by_id[1] = def;
  return ctx;
}

} // namespace

TEST_CASE("breakpoint_matcher matches within current block respecting skip") {
  auto context = make_context();
  test_decoder decoder;
  binja::rewind::core::engine::breakpoint_matcher matcher(&decoder);

  w1::rewind::flow_step current{};
  current.thread_id = 1;
  current.sequence = 5;
  current.block_id = 1;
  current.address = 0x1000;
  current.size = 4;
  current.is_block = true;

  std::unordered_set<uint64_t> breakpoints = {0x1000, 0x1002};
  std::string error;

  auto match = matcher.match_in_current_block(context, current, true, breakpoints, std::nullopt, error);
  CHECK(match.kind == binja::rewind::core::engine::breakpoint_match_kind::exact);
  CHECK(match.address == 0x1000);

  binja::rewind::core::engine::breakpoint_skip skip{0x1000, 5};
  match = matcher.match_in_current_block(context, current, true, breakpoints, skip, error);
  CHECK(match.kind == binja::rewind::core::engine::breakpoint_match_kind::exact);
  CHECK(match.address == 0x1002);
}

TEST_CASE("breakpoint_matcher reports unresolved when decoder missing") {
  auto context = make_context();
  binja::rewind::core::engine::breakpoint_matcher matcher(nullptr);

  w1::rewind::flow_step current{};
  current.thread_id = 1;
  current.sequence = 1;
  current.block_id = 1;
  current.address = 0x1000;
  current.size = 4;
  current.is_block = true;

  std::unordered_set<uint64_t> breakpoints = {0x1000};
  std::string error;

  auto match = matcher.match_in_current_block(context, current, true, breakpoints, std::nullopt, error);
  CHECK(match.kind == binja::rewind::core::engine::breakpoint_match_kind::unresolved_block);
  CHECK(match.address == 0x1000);
}
