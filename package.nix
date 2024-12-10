{
  cmake,
  lib,
  logger,
  stdenv,
  nixfmt-rfc-style,
}:

stdenv.mkDerivation {
  pname = "virtualLink";
  version = "0.0.1";

  outputs = [
    "out"
    "examples"
  ];

  src = ./.;

  nativeBuildInputs = [
    cmake
    nixfmt-rfc-style
  ];

  buildInputs = [
    logger
  ];

  cmakeFlags = [
    "-DBUILD_STATIC_LIBS=ON"
    "-DEXAMPLES_INSTALL_PATH=${placeholder "examples"}"
  ];
}
