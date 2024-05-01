FROM gcc:11 as build

RUN apt-get update && apt-get install --yes python3-pip python-is-python3

RUN pip install "conan<2"
RUN pip install cmake

RUN conan profile new --detect default
RUN conan profile update settings.compiler.libcxx=libstdc++11 default
RUN conan profile update settings.compiler.cppstd=20 default
RUN conan profile update options.rocksdb=False default
RUN conan profile update options.tests=False default

RUN git clone --depth 1 https://github.com/XRPLF/rippled.git
RUN conan install rippled -of build --build missing --settings build_type=Release

RUN cmake -S rippled -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake && \
    cmake --build build --parallel $(nproc) && \
    cmake --install build --prefix /opt/ripple --strip

FROM debian:bullseye-slim as rippled
COPY --from=build /opt/ripple/bin /opt/ripple/bin
COPY --from=build /opt/ripple/etc /opt/ripple/etc

CMD /opt/ripple/bin/rippled --version
