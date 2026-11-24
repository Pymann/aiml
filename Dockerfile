# AIML — reproducible build from the pinned dependency set.
#
# NOT YET VERIFIED: no container runtime was available on the development
# machine, so this file is the host build of BUILDING.md transcribed, not a
# recipe that has been run. Expect to adjust it on first use.
#
#   docker build -t aiml:dev .
#   docker run --rm aiml:dev out/aiml/aiml tests/test.aiml
#
# The image builds the compiler and the runtime. Tests that need a display,
# audio device, network peer or GPU do not run in a plain container.

FROM ubuntu:24.04

# Ubuntu 24.04 is the host the dependency pins in exodus.json were taken from.
# Only what Exodus itself needs: it fetches every project dependency as a
# pinned .deb into __exodus_cache/ and unpacks it there, without root.
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        g++ \
        libx11-dev \
        libxi-dev \
        liblzma-dev \
        libsharpyuv-dev \
        linux-libc-dev \
        git \
        python3 \
        python3-venv \
        python3-pip \
    && rm -rf /var/lib/apt/lists/*

# The build tool. It is NOT the `exodus` package on PyPI (unrelated project).
RUN python3 -m venv /opt/exvenv \
    && /opt/exvenv/bin/pip install --no-cache-dir \
       "git+https://github.com/Pymann/exodus.git"
ENV PATH="/opt/exvenv/bin:${PATH}"

WORKDIR /work
COPY . /work

# apt-get download needs current package lists; the pins are exact versions, so
# a stale list makes the download fail rather than silently pick another build.
RUN apt-get update \
    && exodus pkg install \
    && rm -rf /var/lib/apt/lists/*

# Compiler first, then the runtime it links against; --all afterwards keeps the
# remaining configs in step. The wasm config is skipped here: it needs emsdk,
# which `exodus pkg install --all` would clone (large).
RUN exodus build -c exodus.json \
    && exodus build -c exodus2.json

RUN out/aiml/aiml tests/test.aiml; \
    test $? -eq 42   # tests/test.aiml ends in sys.exit(42)

CMD ["bash"]
