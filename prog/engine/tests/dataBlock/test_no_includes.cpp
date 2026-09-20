// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>
#include <ioSys/dag_memIo.h>

// dblk::ReadFlag::NO_INCLUDES must reject an 'include' directive before the parser
// resolves the path or opens the file, and must not be downgradable by ROBUST.
// See prog/engine/ioSys/dataBlock/blk_parser.cpp.

namespace
{
struct ParseIncludesAsParamsGuard
{
  bool saved = DataBlock::parseIncludesAsParams;
  explicit ParseIncludesAsParamsGuard(bool v) { DataBlock::parseIncludesAsParams = v; }
  ~ParseIncludesAsParamsGuard() { DataBlock::parseIncludesAsParams = saved; }
};

bool load_text(DataBlock &blk, const char *text, dblk::ReadFlags flg)
{
  return dblk::load_text(blk, dag::ConstSpan<char>(text, (int)strlen(text)), flg, "test.blk");
}

bool reported(const ErrorCollector &c, const char *needle)
{
  for (auto &e : c.errors)
    if (strstr(e.text.str(), needle))
      return true;
  return false;
}

const char *save_text(const DataBlock &blk, DynamicMemGeneralSaveCB &cwr)
{
  blk.saveToTextStream(cwr);
  cwr.write("", 1);
  return (const char *)cwr.data();
}

bool writes_include_directive(const DataBlock &blk)
{
  DynamicMemGeneralSaveCB cwr(tmpmem, 0, 1024);
  return strstr(save_text(blk, cwr), "include \"") != nullptr;
}
} // namespace

TEST_CASE("DataBlock NO_INCLUDES rejects include directive", "[datablock][includes]")
{
  FatalFlagsGuard guard;
  guard.setAllNonFatal();
  ErrorCollector collector;
  DataBlock::InstallReporterRAII reporter(&collector);

  DataBlock blk;
  CHECK(load_text(blk, "a:i=1\ninclude \"sub.blk\"\n", dblk::ReadFlag::NO_INCLUDES) == false);
  CHECK(reported(collector, "not allowed"));
}

TEST_CASE("DataBlock NO_INCLUDES is not downgraded by ROBUST", "[datablock][includes]")
{
  FatalFlagsGuard guard;
  guard.setAllNonFatal();
  ErrorCollector collector;
  DataBlock::InstallReporterRAII reporter(&collector);

  DataBlock blk;
  CHECK(load_text(blk, "include \"sub.blk\"\n", dblk::ReadFlag::ROBUST | dblk::ReadFlag::NO_INCLUDES) == false);
  CHECK(reported(collector, "not allowed"));
}

TEST_CASE("DataBlock NO_INCLUDES rejects include on the stream text path", "[datablock][includes]")
{
  FatalFlagsGuard guard;
  guard.setAllNonFatal();
  ErrorCollector collector;
  DataBlock::InstallReporterRAII reporter(&collector);

  const char *text = "a:i=1\r\nb:i=2\r\ninclude \"sub.blk\"\r\n";
  // a stream of 12 bytes or fewer is diverted to loadText, which is already covered above;
  // keeping the include past the probe window also catches parsing only the probed bytes
  REQUIRE(strlen(text) > 12);
  REQUIRE(strstr(text, "include") - text > 12);

  DataBlock blk;
  InPlaceMemLoadCB crd(text, (int)strlen(text));
  CHECK(dblk::load_from_stream(blk, crd, dblk::ReadFlag::NO_INCLUDES, "stream.blk") == false);
  CHECK(reported(collector, "not allowed"));
}

TEST_CASE("DataBlock NO_INCLUDES rejects absolute path without opening it", "[datablock][includes]")
{
  FatalFlagsGuard guard;
  guard.setAllNonFatal();
  ErrorCollector collector;
  DataBlock::InstallReporterRAII reporter(&collector);

  DataBlock blk;
  CHECK(load_text(blk, "include \"/etc/passwd\"\n", dblk::ReadFlag::NO_INCLUDES) == false);
  CHECK(reported(collector, "not allowed"));
  CHECK(!reported(collector, "can't open include file"));
}

TEST_CASE("DataBlock NO_INCLUDES takes precedence over parseIncludesAsParams", "[datablock][includes]")
{
  FatalFlagsGuard guard;
  guard.setAllNonFatal();
  ParseIncludesAsParamsGuard asParams(true);
  ErrorCollector collector;
  DataBlock::InstallReporterRAII reporter(&collector);

  DataBlock blk;
  CHECK(load_text(blk, "include \"sub.blk\"\n", dblk::ReadFlag::NO_INCLUDES) == false);
  CHECK(blk.findParam("@include") < 0);
}

TEST_CASE("DataBlock parseIncludesAsParams still keeps include as param", "[datablock][includes]")
{
  FatalFlagsGuard guard;
  guard.setAllNonFatal();
  ParseIncludesAsParamsGuard asParams(true);

  DataBlock blk;
  REQUIRE(load_text(blk, "include \"sub.blk\"\n", dblk::ReadFlags()));
  REQUIRE(blk.findParam("@include") >= 0);
  CHECK(strcmp(blk.getStr("@include", ""), "sub.blk") == 0);
}

TEST_CASE("DataBlock NO_INCLUDES is sticky", "[datablock][includes]")
{
  FatalFlagsGuard guard;
  guard.setAllNonFatal();

  DataBlock blk;
  REQUIRE(load_text(blk, "a:i=1", dblk::ReadFlag::NO_INCLUDES));
  CHECK((dblk::get_flags(blk) & dblk::ReadFlag::NO_INCLUDES).asInteger() != 0);

  dblk::clr_flag(blk, dblk::ReadFlag::NO_INCLUDES);
  CHECK((dblk::get_flags(blk) & dblk::ReadFlag::NO_INCLUDES).asInteger() == 0);
}

TEST_CASE("DataBlock writes @include param as directive by default", "[datablock][includes]")
{
  DataBlock blk;
  blk.addStr("@include", "sub.blk");
  CHECK(writes_include_directive(blk));
}

TEST_CASE("DataBlock NO_INCLUDES suppresses @include directive on write", "[datablock][includes]")
{
  DataBlock blk;
  blk.addStr("@include", "sub.blk");
  dblk::set_flag(blk, dblk::ReadFlag::NO_INCLUDES);
  CHECK(!writes_include_directive(blk));
}

TEST_CASE("DataBlock binary @include does not re-arm through text conversion", "[datablock][includes]")
{
  FatalFlagsGuard guard;
  guard.setAllNonFatal();
  ErrorCollector collector;
  DataBlock::InstallReporterRAII reporter(&collector);

  DataBlock src;
  src.addStr("@include", "/etc/passwd");
  DynamicMemGeneralSaveCB bin(tmpmem, 0, 1024);
  REQUIRE(src.saveToStream(bin));

  DataBlock dst;
  InPlaceMemLoadCB crd(bin.data(), bin.size());
  REQUIRE(dblk::load_from_stream(dst, crd, dblk::ReadFlag::BINARY_ONLY | dblk::ReadFlag::NO_INCLUDES, "profile.blk"));
  REQUIRE(dst.findParam("@include") >= 0);

  DynamicMemGeneralSaveCB textCwr(tmpmem, 0, 1024);
  const char *text = save_text(dst, textCwr);

  DataBlock reparsed;
  CHECK(load_text(reparsed, text, dblk::ReadFlag::NO_INCLUDES));
  CHECK(reparsed.findParam("@include") >= 0);
}
