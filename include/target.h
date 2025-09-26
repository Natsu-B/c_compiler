#ifndef TARGET_C_COMPILER
#define TARGET_C_COMPILER

// Target selection shared between main.c and generator.c
typedef enum {
  TGT_X86_64 = 0,
  TGT_AARCH64 = 1,
} target_arch;

extern target_arch g_target_arch;

#endif // TARGET_C_COMPILER

