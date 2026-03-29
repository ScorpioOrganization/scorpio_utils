#pragma once
#if ARCH_ARM
  #if DOCKER_BUILD
    #include "magic_enum.hpp"
  #else
    #include "magic_enum/magic_enum.hpp"
  #endif
#else
  #include "magic_enum.hpp"
#endif
