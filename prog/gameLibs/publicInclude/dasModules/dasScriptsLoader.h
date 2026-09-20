//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <daScript/daScript.h>
#include <daScript/ast/ast_serializer.h>
#include <osApiWrappers/dag_files.h>


namespace bind_dascript
{

struct RAIIAlwaysErrorOnException
{
  das::Context *ctx;
  bool was;
  RAIIAlwaysErrorOnException(das::Context *ctx_, bool value = true) : ctx(ctx_)
  {
    was = ctx->alwaysErrorOnException;
    ctx->alwaysErrorOnException = value;
  }
  ~RAIIAlwaysErrorOnException() { ctx->alwaysErrorOnException = was; }
};

struct RAIIAlwaysStackWalkOnException
{
  das::Context *ctx;
  bool was;
  RAIIAlwaysStackWalkOnException(das::Context *ctx_, bool value = true) : ctx(ctx_)
  {
    was = ctx->alwaysStackWalkOnException;
    ctx->alwaysStackWalkOnException = value;
  }
  ~RAIIAlwaysStackWalkOnException() { ctx->alwaysStackWalkOnException = was; }
};


class DebugPrinter final : public das::TextWriter
{
public:
  virtual void output() override
  {
    uint64_t newPos = tellp();
    if (newPos != pos)
    {
      auto len = newPos - pos;
      debug("%.*s", len, data() + pos);
      pos = newPos;
    }
  }

protected:
  int pos = 0;
};


struct FileSerializationStorage : das::SerializationStorage
{
  file_ptr_t file;
  das::string fileName;
  // set when any write was short or failed. The stored checksum then covers bytes that never
  // reached the disk, so the file must never be promoted to a cache - it would read back as a
  // checksum mismatch. Callers writing a cache must check this before keeping the file.
  bool writeFailed = false;

  FileSerializationStorage(file_ptr_t file_, const das::string &name) : file(file_), fileName(name) {}
  virtual size_t writingSize() const override;
  virtual bool readOverflow(void *data, size_t size) override;
  virtual void write(const void *data, size_t size) override;
  // Completes the stream (tail flush and checksum) and makes writeFailed final. Idempotent; the
  // destructor calls it, so a caller that reads writeFailed must call it before destroying this.
  virtual void finish() {}
  virtual ~FileSerializationStorage() override;
};

FileSerializationStorage *create_file_read_serialization_storage(file_ptr_t file, const das::string &name);
FileSerializationStorage *create_file_write_serialization_storage(file_ptr_t file, const das::string &name);

} // namespace bind_dascript
