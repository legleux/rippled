FROM gcc:11 as build

COPY deps.sh deps.sh
COPY config.sh config.sh
COPY build.sh build.sh

RUN ./deps.sh
RUN ./config.sh
RUN ./build.sh
# RUN pip install "conan<2"
# RUN pip install cmake

# RUN conan profile new --detect default
# RUN conan profile update settings.compiler.libcxx=libstdc++11 default
# RUN conan profile update settings.compiler.cppstd=20 default
# RUN conan profile update options.rocksdb=False default
# RUN conan profile update options.tests=False default

# RUN git clone --depth 1 https://github.com/XRPLF/rippled.git
# RUN conan install rippled -of build --build missing --settings build_type=Release

# RUN cmake -S rippled -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake && \
#     cmake --build build --parallel $(nproc) && \
#     cmake --install build --prefix /opt/ripple --strip

FROM debian:bullseye-slim as rippled
COPY --from=build /opt/ripple/bin /opt/ripple/bin
COPY --from=build /opt/ripple/etc /opt/ripple/etc

# Why isn't this linked?
RUN if [ $(uname -m) = "aarch64" ];then apt-get update && apt-get install libatomic1 && apt-get clean && rm -rf /var/cache/apt/lists; fi

CMD /opt/ripple/bin/rippled --version
