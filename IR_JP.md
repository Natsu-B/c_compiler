# Cコンパイラ中間表現 (IR) - 日本語版

[English Version](IR.md)

## 1. 概要

このドキュメントは、このCコンパイラで使用される中間表現（IR）について説明します。
IRは、低レベルの3アドレスコード風の表現です。これは抽象構文木（AST）から生成され、バックエンドがターゲットのアセンブリコードを生成するために使用されます。
IRは無限個の仮想レジスタ（`r`に続く数字、例：`r0`、`r1`）を使用します。

## 2. データ構造

IRは、主に以下の構造で構成されています。

### `IRProgram`
プログラム全体を表します。
- `global_vars`: グローバル変数のための`GlobalVar`構造体のベクタ。
- `functions`: 関数のための`IRFunc`構造体のベクタ。
- `strings`: 文字列リテラルのベクタ。

### `GlobalVar`
グローバル変数を表します。
- `var_name`: 変数名。
- `var_size`: 変数のサイズ（バイト単位）。
- `is_static`: 変数が静的かどうか。
- `initializer`: 変数の初期化方法を記述する`GVarInitializer`構造体のベクタ。

### `GVarInitializer`
グローバル変数の初期化子を記述します。
- `how2_init`: 初期化の種類。
  - `init_zero`: 特定のバイト数をゼロで初期化します。
  - `init_val`: 特定の整数値で初期化します。
  - `init_pointer`: 別のグローバル変数のアドレスで初期化します。
  - `init_string`: 文字列リテラルのアドレスで初期化します。
- `value`: 実際の初期化値または情報。

### `IRFunc`
関数を表します。
- `function_name`: 関数名。
- `stack_size`: 関数のローカル変数に必要なスタックサイズ。
- `num_virtual_regs`: 関数で使用される仮想レジスタの数。
- `is_static`: 関数が静的かどうか。
- `IR_Blocks`: `IR`命令のベクタ。

### `IR`
単一のIR命令を表します。
- `kind`: 命令の種類を指定する`IRKind`列挙値。
- 各命令種別のための特定のフィールドを含む`union`。

## 3. 命令セット (`IRKind`)

以下は、すべてのIR命令のリストです。`rX`は仮想レジスタ、`imm`は即値、`label`はラベル名、`func`は関数名を示します。

### 関数命令
- **`CALL r_dst, func, (r_arg1, r_arg2, ...)`**
  - `IR_CALL`: 関数を呼び出します。
  - `dst_reg`: 戻り値の格納先レジスタ。
  - `func_name`: 呼び出す関数名。
  - `args`: 引数のためのレジスタのベクタ。
- **`PROLOGUE`**
  - `IR_FUNC_PROLOGUE`: 関数のスタックフレームを設定します。
- **`EPILOGUE`**
  - `IR_FUNC_EPILOGUE`: 関数のスタックフレームを破棄します。
- **`RET r_src`**
  - `IR_RET`: 関数から戻ります。
  - `src_reg`: 戻り値を含むレジスタ。

### メモリ操作
- **`MOV r_dst, r_src`** / **`MOV r_dst, imm`**
  - `IR_MOV`: ソースレジスタまたは即値からデスティネーションレジスタに値を移動します。
  - `dst_reg`: デスティネーションレジスタ。
  - `src_reg`: ソースレジスタ（即値でない場合）。
  - `imm_val`: 即値（`is_imm`がtrueの場合）。
- **`LOAD <size> r_dst, [r_addr + offset]`**
  - `IR_LOAD`: メモリから値をロードします。
  - `reg`: デスティネーションレジスタ。
  - `mem_reg`: ベースメモリアドレスを保持するレジスタ。
  - `offset`: ベースアドレスからのオフセット。
  - `size`: ロードするデータのサイズ（BYTE, WORD, DWORD, QWORD）。
- **`STORE <size> [r_addr + offset], r_src`**
  - `IR_STORE`: メモリに値をストアします。
  - `mem_reg`: ベースメモリアドレスを保持するレジスタ。
  - `offset`: ベースアドレスからのオフセット。
  - `reg`: ストアする値を含むソースレジスタ。
  - `size`: ストアするデータのサイズ。
- **`STORE_ARG <size> r_dst, arg_idx`**
  - `IR_STORE_ARG`: 物理レジスタ（またはスタック）からスタック上の仮想レジスタに関数の引数を格納します。
  - `dst_reg`: スタック上のデスティネーションレジスタ（アドレス）。
  - `arg_index`: 引数のインデックス。
  - `size`: 引数のサイズ。
- **`LEA r_dst, [address]`**
  - `IR_LEA`: 変数の実効アドレスをレジスタにロードします。
  - `dst_reg`: デスティネーションレジスタ。
  - `address`: ローカル変数（`LOCAL offset`）、静的変数（`STATIC name`）、またはグローバル変数（`GLOBAL name`）を指定できます。

### 算術演算
- **`ADD <size> r_dst, r_lhs, r_rhs`**
  - `IR_ADD`: 加算。
- **`SUB <size> r_dst, r_lhs, r_rhs`**
  - `IR_SUB`: 減算。
- **`MUL <size> r_dst, r_lhs, r_rhs`**
  - `IR_MUL`: 乗算。
- **`DIV <size> r_dst, r_lhs, r_rhs`**
  - `IR_OP_DIV`: 符号なし除算。
- **`IDIV <size> r_dst, r_lhs, r_rhs`**
  - `IR_OP_IDIV`: 符号付き除算（剰余）。
- **引数:**
  - `dst_reg`: デスティネーションレジスタ。
  - `lhs_reg`, `rhs_reg`: ソースレジスタ。
  - `size`: オペランドサイズ。

### 比較演算
- **`EQ <size> r_dst, r_lhs, r_rhs`**
  - `IR_EQ`: 等しい。
- **`NEQ <size> r_dst, r_lhs, r_rhs`**
  - `IR_NEQ`: 等しくない。
- **`LT <size> r_dst, r_lhs, r_rhs`**
  - `IR_LT`: より小さい。
- **`LTE <size> r_dst, r_lhs, r_rhs`**
  - `IR_LTE`: 以下。
- **引数:**
  - `dst_reg`: デスティネーションレジスタ（trueの場合は1、falseの場合は0を受け取ります）。
  - `lhs_reg`, `rhs_reg`: ソースレジスタ。
  - `size`: オペランドサイズ。

### ジャンプ命令
- **`JMP label`**
  - `IR_JMP`: 無条件ジャンプ。
  - `label`: ターゲットラベル。
- **`JE label, r_cond`**
  - `IR_JE`: 等しい場合にジャンプ（`r_cond`がゼロの場合）。
  - `label`: ターゲットラベル。
  - `cond_reg`: チェックするレジスタ。
- **`JNE label, r_cond`**
  - `IR_JNE`: 等しくない場合にジャンプ（`r_cond`がゼロでない場合）。
  - `label`: ターゲットラベル。
  - `cond_reg`: チェックするレジスタ。

### ビット演算
- **`AND <size> r_dst, r_lhs, r_rhs`**
  - `IR_AND`: ビット単位AND。
- **`OR <size> r_dst, r_lhs, r_rhs`**
  - `IR_OR`: ビット単位OR。
- **`XOR <size> r_dst, r_lhs, r_rhs`**
  - `IR_XOR`: ビット単位XOR。
- **`SHL <size> r_dst, r_lhs, r_rhs`**
  - `IR_SHL`: 左シフト（符号なし）。
- **`SHR <size> r_dst, r_lhs, r_rhs`**
  - `IR_SHR`: 右シフト（符号なし）。
- **`SAL <size> r_dst, r_lhs, r_rhs`**
  - `IR_SAL`: 算術左シフト（符号付き）。
- **`SAR <size> r_dst, r_lhs, r_rhs`**
  - `IR_SAR`: 算術右シフト（符号付き）。
- **引数:**
  - `dst_reg`: デスティネーションレジスタ。
  - `lhs_reg`, `rhs_reg`: ソースレジスタ。
  - `size`: オペランドサイズ。

### 単項演算
- **`NEG r_dst, r_src`**
  - `IR_NEG`: 符号反転。
- **`NOT r_dst, r_src`**
  - `IR_NOT`: 論理NOT。
- **`BNOT r_dst, r_src`**
  - `IR_BIT_NOT`: ビット単位NOT。
- **引数:**
  - `dst_reg`: デスティネーションレジスタ。
  - `src_reg`: ソースレジスタ。

### ラベルと組み込み
- **`label:`**
  - `IR_LABEL`: ジャンプターゲットを定義します。
  - `name`: ラベル名。
- **`ASM "..."`**
  - `IR_BUILTIN_ASM`: インラインアセンブリ。
  - `asm_str`: アセンブリコード文字列。

### グローバルデータ
- **`GVAR name size`**
  - グローバル変数を定義します。
  - 初期化子の行が続きます：`ZERO <len>`, `VAL <size> <value>`, `VAR <name>`, `STRING <label>`。
- **`STRING name "..."`**
  - 文字列リテラルを定義します。
