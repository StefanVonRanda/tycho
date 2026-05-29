# Static-build image for Hier-generated C.
# Alpine's gcc targets musl libc, so `gcc -static` yields a fully static
# ELF binary with no shared-library dependencies.
FROM alpine:3.20
RUN apk add --no-cache gcc musl-dev file
WORKDIR /work
