#include <filesystem>
#include <memory>
#include <unordered_set>

#include "doctest/doctest.hpp"

#include "rewind/core/engine/run_loop.hpp"
#include "w1base/arch_spec.hpp"
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

trace_bundle build_trace(const char* name, uint64_t count) {
  namespace fs = std::filesystem;
  trace_bundle out;
  out.trace_path = fs::temp_directory_path() / name;
  out.index_path = fs::temp_directory_path() / (std::string(name) + ".idx");

  out.writer_config.path = out.trace_path.string();
  out.writer_config.log = redlog::get_logger("test.rewind_core.run_loop");
  out.writer_config.chunk_size = 64;

  auto writer = w1::rewind::make_trace_file_writer(out.writer_config);
  REQUIRE(writer);
  REQUIRE(writer->open());

  w1::arch::arch_spec arch{};
  std::string arch_error;
  REQUIRE(w1::arch::parse_arch_spec("x86_64", arch, arch_error));

  w1::rewind::trace_header header{};
  header.arch = arch;
  header.flags = w1::rewind::trace_flag_instructions;
  REQUIRE(writer->write_header(header));

  w1::rewind::target_info_record target{};
  target.os = "test";
  target.abi = "test";
  target.cpu = "test";
  REQUIRE(writer->write_target_info(target));

  w1::rewind::target_environment_record env{};
  env.os_version = "1.0";
  env.os_build = "test";
  env.os_kernel = "test";
  env.hostname = "test";
  env.pid = 1;
  env.addressing_bits = 48;
  env.low_mem_addressing_bits = 48;
  env.high_mem_addressing_bits = 48;
  REQUIRE(writer->write_target_environment(env));

  w1::rewind::register_spec_record regs{};
  REQUIRE(writer->write_register_spec(regs));

  w1::rewind::module_table_record modules{};
  REQUIRE(writer->write_module_table(modules));

  w1::rewind::thread_start_record start{};
  start.thread_id = 1;
  start.name = "thread1";
  REQUIRE(writer->write_thread_start(start));

  for (uint64_t i = 0; i < count; ++i) {
    w1::rewind::instruction_record inst{};
    inst.thread_id = 1;
    inst.sequence = i;
    inst.address = 0x1000 + i * 4;
    inst.size = 4;
    inst.flags = 0;
    REQUIRE(writer->write_instruction(inst));
  }

  w1::rewind::thread_end_record end{};
  end.thread_id = 1;
  REQUIRE(writer->write_thread_end(end));

  writer->flush();
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
  out.writer_config.chunk_size = 64;

  auto writer = w1::rewind::make_trace_file_writer(out.writer_config);
  REQUIRE(writer);
  REQUIRE(writer->open());

  w1::arch::arch_spec arch{};
  std::string arch_error;
  REQUIRE(w1::arch::parse_arch_spec("x86_64", arch, arch_error));

  w1::rewind::trace_header header{};
  header.arch = arch;
  header.flags = w1::rewind::trace_flag_blocks;
  REQUIRE(writer->write_header(header));

  w1::rewind::target_info_record target{};
  target.os = "test";
  target.abi = "test";
  target.cpu = "test";
  REQUIRE(writer->write_target_info(target));

  w1::rewind::target_environment_record env{};
  env.os_version = "1.0";
  env.os_build = "test";
  env.os_kernel = "test";
  env.hostname = "test";
  env.pid = 1;
  env.addressing_bits = 48;
  env.low_mem_addressing_bits = 48;
  env.high_mem_addressing_bits = 48;
  REQUIRE(writer->write_target_environment(env));

  w1::rewind::register_spec_record regs{};
  REQUIRE(writer->write_register_spec(regs));

  w1::rewind::module_table_record modules{};
  REQUIRE(writer->write_module_table(modules));

  w1::rewind::thread_start_record start{};
  start.thread_id = 1;
  start.name = "thread1";
  REQUIRE(writer->write_thread_start(start));

  w1::rewind::block_definition_record def1{};
  def1.block_id = 1;
  def1.address = 0x1000;
  def1.size = 4;
  REQUIRE(writer->write_block_definition(def1));

  w1::rewind::block_definition_record def2{};
  def2.block_id = 2;
  def2.address = 0x2000;
  def2.size = 4;
  REQUIRE(writer->write_block_definition(def2));

  w1::rewind::block_exec_record exec1{};
  exec1.thread_id = 1;
  exec1.sequence = 0;
  exec1.block_id = 1;
  REQUIRE(writer->write_block_exec(exec1));

  w1::rewind::block_exec_record exec2{};
  exec2.thread_id = 1;
  exec2.sequence = 1;
  exec2.block_id = 2;
  REQUIRE(writer->write_block_exec(exec2));

  w1::rewind::thread_end_record end{};
  end.thread_id = 1;
  REQUIRE(writer->write_thread_end(end));

  writer->flush();
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
