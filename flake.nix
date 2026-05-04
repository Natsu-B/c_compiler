{
  description = "Dev shells for this C compiler: x86_64 and AArch64 cross";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        cross = pkgs.pkgsCross.aarch64-multiplatform;
        aarch64Cc = "${cross.stdenv.cc}/bin/${cross.stdenv.cc.targetPrefix}gcc";
      in
      {
        devShells = {
          default = pkgs.mkShell {
            name = "c-compiler-devshell";
            packages = with pkgs; [
              gcc
              gnumake
              binutils
              qemu
              ripgrep
            ];
            shellHook = ''
              set -e
              DEV_BIN="$PWD/.devtools/bin"
              mkdir -p "$DEV_BIN"
              # Provide a conventional cross-cc name for AArch64
              ln -sf ${aarch64Cc} "$DEV_BIN/aarch64-linux-gnu-gcc"
              # Also expose target-prefixed toolchain binaries when available
              for tool in gcc g++ ar ranlib as ld objdump objcopy; do
                if [ -x "${cross.stdenv.cc}/bin/${cross.stdenv.cc.targetPrefix}$tool" ]; then
                  ln -sf "${cross.stdenv.cc}/bin/${cross.stdenv.cc.targetPrefix}$tool" \
                         "$DEV_BIN/${cross.stdenv.cc.targetPrefix}$tool"
                fi
              done
              export PATH="$DEV_BIN:$PATH"
              export AARCH64_CC=aarch64-linux-gnu-gcc
              echo "[devshell default] AARCH64_CC=$AARCH64_CC (cross tool shims active)"
              echo "[devshell default] Use 'make test-aarch64' to run cross tests."
            '';
          };
        };
      }
    );
}
