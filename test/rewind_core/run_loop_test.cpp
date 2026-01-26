#include <filesystem>
#include <memory>
#include <unordered_set>

#include "doctest/doctest.hpp"

#include "rewind/core/engine/run_loop.hpp"
#include "w1base/arch_spec.hpp"
#include "w1rewind/record/trace_builder.hpp"
#include "w1rewind/replay/flow_cursor.hpp"
#include "w1rewind/replay/replay_context.hpp"
#include "w1rewind/trace/trace_file_writer.hpp"
#include "w1rewind/trace/trace_index.hpp"
#include "w1rewind/trace/trace_reader.hpp"

namespace {

struct trace_bundle {
  std::filesystem::path trace_path;
  std::filesystem::path index_path;
  w1::rewind::trace_file_writer_config writer_config;
  std::shared_ptr<w1::rewind::trace_index> index;
  w1::rewind::replay_context context;
  std::shared_ptr<w1::rewind::trace_reader> stream;
};

w1::rewind::endian to_endian(w1::arch::byte_order order) {
  switch (order) {
  case w1::arch::byte_order::little:
    return w1::rewind::endian::little;
  case w1::arch::byte_order::big:
    return w1::rewind::endian::big;
  default:
    return w1::rewind::endian::unknown;
  }
}

w1::rewind::file_header make_header() {
  w1::rewind::file_header header{};
  header.trace_uuid[0] = 1;
  return header;
}

w1::rewind::arch_descriptor_record make_arch_descriptor(const w1::arch::arch_spec& arch) {
  w1::rewind::arch_descriptor_record record{};
  record.arch_id = "x86_64";
  record.byte_order = to_endian(arch.arch_byte_order);
  uint16_t bits = static_cast<uint16_t>(arch.pointer_bits != 0 ? arch.pointer_bits : 64);
  record.pointer_bits = bits;
  record.address_bits = bits;
  record.gdb_arch = std::string(w1::arch::gdb_arch_name(arch));
  record.gdb_feature = std::string(w1::arch::gdb_feature_name(arch));
  record.modes.push_back({0, record.arch_id});
  return record;
}

w1::rewind::environment_record make_environment() {
  w1::rewind::environment_record env{};
  env.os_id = "test";
  env.abi = "test";
  env.cpu = "test";
  env.hostname = "test";
  env.pid = 1;
  return env;
}

w1::rewind::address_space_record make_address_space(const w1::arch::arch_spec& arch) {
  w1::rewind::address_space_record space{};
  space.space_id = 0;
  space.name = "default";
  space.address_bits = static_cast<uint16_t>(arch.pointer_bits != 0 ? arch.pointer_bits : 64);
  space.byte_order = to_endian(arch.arch_byte_order);
  return space;
}

trace_bundle build_trace(const char* name, uint64_t count) {
  namespace fs = std::filesystem;
  trace_bundle out;
  out.trace_path = fs::temp_directory_path() / name;
  out.index_path = fs::temp_directory_path() / (std::string(name) + ".idx");

  out.writer_config.path = out.trace_path.string();
  out.writer_config.log = redlog::get_logger("test.rewind_core.run_loop");
  out.writer_config.chunk_size = 512;

  auto writer = w1::rewind::make_trace_file_writer(out.writer_config);
  REQUIRE(writer);
  REQUIRE(writer->open());

  w1::arch::arch_spec arch{};
  std::string arch_error;
  REQUIRE(w1::arch::parse_arch_spec("x86_64", arch, arch_error));

  w1::rewind::trace_builder_config builder_config{writer, out.writer_config.log};
  w1::rewind::trace_builder builder(builder_config);

  auto header = make_header();
  REQUIRE(builder.begin_trace(header));
  auto arch_desc = make_arch_descriptor(arch);
  REQUIRE(builder.emit_arch_descriptor_checked(arch_desc));
  auto env = make_environment();
  REQUIRE(builder.emit_environment_checked(env));
  auto space = make_address_space(arch);
  REQUIRE(builder.emit_address_space(space));

  REQUIRE(builder.begin_thread(1, "thread1"));

  for (uint64_t i = 0; i < count; ++i) {
    uint64_t sequence = 0;
    REQUIRE(builder.emit_instruction(1, 0x1000 + i * 4, 4, space.space_id, 0, sequence));
  }

  REQUIRE(builder.end_thread(1));

  builder.flush();
  writer->close();

  w1::rewind::trace_index_options options;
  out.index = std::make_shared<w1::rewind::trace_index>();
  REQUIRE(
      w1::rewind::build_trace_index(
          out.trace_path.string(), out.index_path.string(), options, out.index.get(), out.writer_config.log
      )
  );

  std::string error;
  REQUIRE(w1::rewind::load_replay_context(out.trace_path.string(), out.context, error));
  out.stream = std::make_shared<w1::rewind::trace_reader>(out.trace_path.string());
  return out;
}

w1::rewind::flow_cursor make_cursor(trace_bundle& trace, size_t history_size) {
  w1::rewind::record_stream_cursor stream_cursor(trace.stream);
  w1::rewind::flow_extractor extractor(&trace.context);
  w1::rewind::history_window history(history_size);
  return w1::rewind::flow_cursor(std::move(stream_cursor), std::move(extractor), std::move(history), trace.index);
}

trace_bundle build_block_trace(const char* name) {
  namespace fs = std::filesystem;
  trace_bundle out;
  out.trace_path = fs::temp_directory_path() / name;
  out.index_path = fs::temp_directory_path() / (std::string(name) + ".idx");

  out.writer_config.path = out.trace_path.string();
  out.writer_config.log = redlog::get_logger("test.rewind_core.run_loop");
  out.writer_config.chunk_size = 512;

  auto writer = w1::rewind::make_trace_file_writer(out.writer_config);
  REQUIRE(writer);
  REQUIRE(writer->open());

  w1::arch::arch_spec arch{};
  std::string arch_error;
  REQUIRE(w1::arch::parse_arch_spec("x86_64", arch, arch_error));

  w1::rewind::trace_builder_config builder_config{writer, out.writer_config.log};
  w1::rewind::trace_builder builder(builder_config);

  auto header = make_header();
  REQUIRE(builder.begin_trace(header));
  auto arch_desc = make_arch_descriptor(arch);
  REQUIRE(builder.emit_arch_descriptor_checked(arch_desc));
  auto env = make_environment();
  REQUIRE(builder.emit_environment_checked(env));
  auto space = make_address_space(arch);
  REQUIRE(builder.emit_address_space(space));

  REQUIRE(builder.begin_thread(1, "thread1"));

  uint64_t sequence = 0;
  REQUIRE(builder.emit_block(1, 0x1000, 4, space.space_id, 0, sequence));
  REQUIRE(builder.emit_block(1, 0x2000, 4, space.space_id, 0, sequence));

  REQUIRE(builder.end_thread(1));

  builder.flush();
  writer->close();

  w1::rewind::trace_index_options options;
  out.index = std::make_shared<w1::rewind::trace_index>();
  REQUIRE(
      w1::rewind::build_trace_index(
          out.trace_path.string(), out.index_path.string(), options, out.index.get(), out.writer_config.log
      )
  );

  std::string error;
  REQUIRE(w1::rewind::load_replay_context(out.trace_path.string(), out.context, error));
  out.stream = std::make_shared<w1::rewind::trace_reader>(out.trace_path.string());
  return out;
}

} // namespace

TEST_CASE("run_loop stops at breakpoint") {
  auto trace = build_trace("rewind_core_run_loop_bp.trace", 6);
  auto cursor = make_cursor(trace, 4);
  REQUIRE(cursor.open());
  REQUIRE(cursor.seek(1, 0));

  w1::rewind::flow_step step{};
  REQUIRE(cursor.step_forward(step));
  CHECK(step.sequence == 0);

  binja::rewind::core::engine::breakpoint_matcher matcher(nullptr);
  binja::rewind::core::engine::run_loop loop(&cursor, &matcher, &trace.context);

  std::unordered_set<uint64_t> breakpoints = {0x1000 + 3 * 4};
  auto stop = loop.run(true, breakpoints, std::nullopt, []() { return false; });

  CHECK(stop.reason == binja::rewind::core::engine::run_stop_reason::hit_exact);
  REQUIRE(stop.hit_address.has_value());
  CHECK(*stop.hit_address == 0x1000 + 3 * 4);
  REQUIRE(stop.last_step.has_value());
  CHECK(stop.last_step->sequence == 3);
}

TEST_CASE("run_loop reports begin_of_trace when stepping backward") {
  auto trace = build_trace("rewind_core_run_loop_back.trace", 3);
  auto cursor = make_cursor(trace, 4);
  REQUIRE(cursor.open());
  REQUIRE(cursor.seek(1, 0));

  w1::rewind::flow_step step{};
  REQUIRE(cursor.step_forward(step));
  REQUIRE(cursor.step_forward(step));
  REQUIRE(cursor.step_forward(step));
  CHECK(step.sequence == 2);

  binja::rewind::core::engine::breakpoint_matcher matcher(nullptr);
  binja::rewind::core::engine::run_loop loop(&cursor, &matcher, &trace.context);

  auto stop = loop.run(false, {}, std::nullopt, []() { return false; });
  CHECK(stop.reason == binja::rewind::core::engine::run_stop_reason::begin_of_trace);
  REQUIRE(stop.last_step.has_value());
  CHECK(stop.last_step->sequence == 0);
}

TEST_CASE("run_loop cancels before stepping") {
  auto trace = build_trace("rewind_core_run_loop_cancel.trace", 2);
  auto cursor = make_cursor(trace, 2);
  REQUIRE(cursor.open());
  REQUIRE(cursor.seek(1, 0));

  w1::rewind::flow_step step{};
  REQUIRE(cursor.step_forward(step));

  binja::rewind::core::engine::breakpoint_matcher matcher(nullptr);
  binja::rewind::core::engine::run_loop loop(&cursor, &matcher, &trace.context);

  auto stop = loop.run(true, {}, std::nullopt, []() { return true; });
  CHECK(stop.reason == binja::rewind::core::engine::run_stop_reason::cancelled);
  CHECK_FALSE(stop.last_step.has_value());
}

TEST_CASE("run_loop skips current breakpoint when running backward on block trace") {
  struct test_decoder final : public w1::rewind::block_decoder {
    bool decode_block(
        const w1::rewind::replay_context&, const w1::rewind::flow_step& flow, w1::rewind::decoded_block& out,
        std::string&
    ) override {
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

  auto trace = build_block_trace("rewind_core_run_loop_block.trace");
  auto cursor = make_cursor(trace, 4);
  REQUIRE(cursor.open());
  REQUIRE(cursor.seek(1, 1));

  w1::rewind::flow_step step{};
  REQUIRE(cursor.step_forward(step));
  CHECK(step.sequence == 1);

  test_decoder decoder;
  binja::rewind::core::engine::breakpoint_matcher matcher(&decoder);
  binja::rewind::core::engine::run_loop loop(&cursor, &matcher, &trace.context);

  std::unordered_set<uint64_t> breakpoints = {0x2000};
  binja::rewind::core::engine::breakpoint_skip skip{0x2000, 1};
  auto stop = loop.run(false, breakpoints, skip, []() { return false; });

  CHECK(stop.reason == binja::rewind::core::engine::run_stop_reason::begin_of_trace);
  REQUIRE(stop.last_step.has_value());
  CHECK(stop.last_step->sequence == 0);
  CHECK_FALSE(stop.hit_address.has_value());
}
