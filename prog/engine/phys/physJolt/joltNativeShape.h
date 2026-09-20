// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <phys/dag_physCollision.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

// PhysCollision that carries a ready JPH shape (TYPE_NATIVE_SHAPE). localScale is the scale still to
// apply at body build (identity for a shape scaled already), never a record of one applied.
struct JoltPhysNativeShape : public PhysCollision
{
  JPH::RefConst<JPH::Shape> shape;
  JPH::Vec3 localScale;

  JoltPhysNativeShape(JPH::RefConst<JPH::Shape> s, JPH::Vec3 ls = JPH::Vec3::sReplicate(1.f)) :
    PhysCollision(TYPE_NATIVE_SHAPE), shape(s), localScale(ls)
  {}
};
