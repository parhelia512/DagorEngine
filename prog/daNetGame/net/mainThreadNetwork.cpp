// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "net.h"


NetworkVariant network_variant() { return NetworkVariant::MainNet; }

bool is_main_thread_network() { return true; }
