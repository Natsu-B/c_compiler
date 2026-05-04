# C Compiler Intermediate Representation (IR)

[日本語版](IR_JP.md)

## 1. Overview

This document describes the Intermediate Representation (IR) used in this C compiler.
The IR is a low-level, three-address code like representation. It is generated from the Abstract Syntax Tree (AST) and is used by the backend to generate target assembly code.
It uses an infinite number of virtual registers (denoted as `r` followed by a number, e.g., `r0`, `r1`).

## 2. Data Structures

The IR is organized into the following main structures:

### `IRProgram`
Represents the entire program.
- `global_vars`: A vector of `GlobalVar` structs for global variables.
- `functions`: A vector of `IRFunc` structs for functions.
- `strings`: A vector of string literals.

### `GlobalVar`
Represents a global variable.
- `var_name`: Name of the variable.
- `var_size`: Size of the variable in bytes.
- `is_static`: Whether the variable is static.
- `initializer`: A vector of `GVarInitializer` structs describing how the variable is initialized.

### `GVarInitializer`
Describes the initializer for a global variable.
- `how2_init`: The type of initialization.
  - `init_zero`: Zero-initialize a certain number of bytes.
  - `init_val`: Initialize with a specific integer value.
  - `init_pointer`: Initialize with the address of another global variable.
  - `init_string`: Initialize with the address of a string literal.
- `value`: The actual initialization value or information.

### `IRFunc`
Represents a function.
- `function_name`: Name of the function.
- `stack_size`: The stack size required for the function's local variables.
- `num_virtual_regs`: The number of virtual registers used in the function.
- `is_static`: Whether the function is static.
- `IR_Blocks`: A vector of `IR` instructions.

### `IR`
Represents a single IR instruction.
- `kind`: An `IRKind` enum value that specifies the type of the instruction.
- A `union` containing specific fields for each instruction kind.

## 3. Instruction Set (`IRKind`)

The following is a list of all IR instructions. `rX` denotes a virtual register, `imm` an immediate value, `label` a label name, and `func` a function name.

### Function Instructions
- **`CALL r_dst, func, (r_arg1, r_arg2, ...)`**
  - `IR_CALL`: Calls a function.
  - `dst_reg`: Destination register for the return value.
  - `func_name`: Name of the function to call.
  - `args`: A vector of registers for the arguments.
- **`PROLOGUE`**
  - `IR_FUNC_PROLOGUE`: Sets up the function stack frame.
- **`EPILOGUE`**
  - `IR_FUNC_EPILOGUE`: Tears down the function stack frame.
- **`RET r_src`**
  - `IR_RET`: Returns from a function.
  - `src_reg`: The register containing the return value.

### Memory Operations
- **`MOV r_dst, r_src`** / **`MOV r_dst, imm`**
  - `IR_MOV`: Moves a value from a source register or an immediate value to a destination register.
  - `dst_reg`: Destination register.
  - `src_reg`: Source register (if not immediate).
  - `imm_val`: Immediate value (if `is_imm` is true).
- **`LOAD <size> r_dst, [r_addr + offset]`**
  - `IR_LOAD`: Loads a value from memory.
  - `reg`: Destination register.
  - `mem_reg`: Register holding the base memory address.
  - `offset`: Offset from the base address.
  - `size`: Size of the data to load (BYTE, WORD, DWORD, QWORD).
- **`STORE <size> [r_addr + offset], r_src`**
  - `IR_STORE`: Stores a value to memory.
  - `mem_reg`: Register holding the base memory address.
  - `offset`: Offset from the base address.
  - `reg`: Source register containing the value to store.
  - `size`: Size of the data to store.
- **`STORE_ARG <size> r_dst, arg_idx`**
  - `IR_STORE_ARG`: Stores a function argument from a physical register (or stack) into a virtual register on the stack.
  - `dst_reg`: The destination register (address) on the stack.
  - `arg_index`: The index of the argument.
  - `size`: The size of the argument.
- **`LEA r_dst, [address]`**
  - `IR_LEA`: Loads the effective address of a variable into a register.
  - `dst_reg`: Destination register.
  - `address`: Can be a local variable (`LOCAL offset`), a static variable (`STATIC name`), or a global variable (`GLOBAL name`).

### Arithmetic Operations
- **`ADD <size> r_dst, r_lhs, r_rhs`**
  - `IR_ADD`: Addition.
- **`SUB <size> r_dst, r_lhs, r_rhs`**
  - `IR_SUB`: Subtraction.
- **`MUL <size> r_dst, r_lhs, r_rhs`**
  - `IR_MUL`: Multiplication.
- **`DIV <size> r_dst, r_lhs, r_rhs`**
  - `IR_OP_DIV`: Unsigned division.
- **`IDIV <size> r_dst, r_lhs, r_rhs`**
  - `IR_OP_IDIV`: Signed division (remainder).
- **Arguments:**
  - `dst_reg`: Destination register.
  - `lhs_reg`, `rhs_reg`: Source registers.
  - `size`: Operand size.

### Comparison Operations
- **`EQ <size> r_dst, r_lhs, r_rhs`**
  - `IR_EQ`: Equal.
- **`NEQ <size> r_dst, r_lhs, r_rhs`**
  - `IR_NEQ`: Not equal.
- **`LT <size> r_dst, r_lhs, r_rhs`**
  - `IR_LT`: Less than.
- **`LTE <size> r_dst, r_lhs, r_rhs`**
  - `IR_LTE`: Less than or equal.
- **Arguments:**
  - `dst_reg`: Destination register (receives 1 for true, 0 for false).
  - `lhs_reg`, `rhs_reg`: Source registers.
  - `size`: Operand size.

### Jump Instructions
- **`JMP label`**
  - `IR_JMP`: Unconditional jump.
  - `label`: The target label.
- **`JE label, r_cond`**
  - `IR_JE`: Jump if equal (if `r_cond` is zero).
  - `label`: The target label.
  - `cond_reg`: The register to check.
- **`JNE label, r_cond`**
  - `IR_JNE`: Jump if not equal (if `r_cond` is not zero).
  - `label`: The target label.
  - `cond_reg`: The register to check.

### Bitwise Operations
- **`AND <size> r_dst, r_lhs, r_rhs`**
  - `IR_AND`: Bitwise AND.
- **`OR <size> r_dst, r_lhs, r_rhs`**
  - `IR_OR`: Bitwise OR.
- **`XOR <size> r_dst, r_lhs, r_rhs`**
  - `IR_XOR`: Bitwise XOR.
- **`SHL <size> r_dst, r_lhs, r_rhs`**
  - `IR_SHL`: Shift left (unsigned).
- **`SHR <size> r_dst, r_lhs, r_rhs`**
  - `IR_SHR`: Shift right (unsigned).
- **`SAL <size> r_dst, r_lhs, r_rhs`**
  - `IR_SAL`: Shift arithmetic left (signed).
- **`SAR <size> r_dst, r_lhs, r_rhs`**
  - `IR_SAR`: Shift arithmetic right (signed).
- **Arguments:**
  - `dst_reg`: Destination register.
  - `lhs_reg`, `rhs_reg`: Source registers.
  - `size`: Operand size.

### Unary Operations
- **`NEG r_dst, r_src`**
  - `IR_NEG`: Negation.
- **`NOT r_dst, r_src`**
  - `IR_NOT`: Logical NOT.
- **`BNOT r_dst, r_src`**
  - `IR_BIT_NOT`: Bitwise NOT.
- **Arguments:**
  - `dst_reg`: Destination register.
  - `src_reg`: Source register.

### Labels and Built-ins
- **`label:`**
  - `IR_LABEL`: Defines a jump target.
  - `name`: The name of the label.
- **`ASM "..."`**
  - `IR_BUILTIN_ASM`: In-line assembly.
  - `asm_str`: The assembly code string.

### Global Data
- **`GVAR name size`**
  - Defines a global variable.
  - Followed by initializer lines: `ZERO <len>`, `VAL <size> <value>`, `VAR <name>`, `STRING <label>`.
- **`STRING name "..."`**
  - Defines a string literal.
