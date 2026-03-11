set PATH=C:\QTIDE\Tools\mingw1310_64\bin;%PATH%
cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/QTIDE/6.10.2/mingw_64"
cmake --build build -j 4
