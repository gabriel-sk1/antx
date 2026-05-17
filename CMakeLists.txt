cmake_minimum_required(VERSION 3.15)
project(antx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configurações globais para compilação no Windows (MSVC/MinGW)
if(MSVC)
    add_compile_options(/W4 /WX /EHsc)
    # Impede pop-ups do linker para o console se formos compilar serviços com subsistema gui, 
    # mas manteremos console para facilitar logs se rodar em modo depuração.
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Diretórios de include
include_directories(src/common)

# 1. Executável do Serviço antx_service
add_executable(antx_service
    src/service/service.cpp
)
target_link_libraries(antx_service
    PRIVATE
    bcrypt
    advapi32
    user32
)

# 2. Executável da CLI antx
add_executable(antx
    src/cli/main.cpp
)
target_link_libraries(antx
    PRIVATE
    bcrypt
    advapi32
)

# 3. Executável do Painel GUI antx_monitor
add_executable(antx_monitor
    src/monitor/monitor.cpp
)
target_link_libraries(antx_monitor
    PRIVATE
    gdiplus
    gdi32
    user32
    advapi32
    psapi
)
if(NOT MSVC)
    target_link_options(antx_monitor PRIVATE -mwindows)
endif()

# Configura diretórios de saída dos executáveis para a pasta build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
