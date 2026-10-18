cmake_minimum_required(VERSION 3.26)

project(
    Desbordante.bindings
    LANGUAGES CXX
    DESCRIPTION "Python bindings to the interface of the Desbordante C++ core library"
)

# nanobind 3.1.0 is the latest stable release at the start of the migration. It requires C++17
# and Python >= 3.10 and supports linked Stable ABI builds (STABLE_ABI) on CPython >= 3.12,
# which the previous binding generator could not do. The probe targets below are only toolchain checks:
# they are not installed, so the wheel still contains only the desbordante module.
find_package(
    Python 3.10 REQUIRED
    COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule
)

CPMAddPackage("gh:Tessil/robin-map@1.4.1")
CPMAddPackage(URI "gh:wjakob/nanobind@3.1.0" OPTIONS "NB_USE_SUBMODULE_DEPS OFF")
nanobind_build_library(nanobind-static)

# ---- Python Bindings Configuration ----

# Define the output name for Python bindings module
# NOTE: Matches the Python import name (e.g. 'import desbordante')
set(MODULE_NAME
    "desbordante"
    CACHE INTERNAL ""
)

nanobind_add_module(${MODULE_NAME} NB_STATIC bind_main_classes.cpp bindings.cpp)
target_link_libraries(
    ${MODULE_NAME} PRIVATE Desbordante::algos spdlog::spdlog_header_only Boost::headers
                           Desbordante.bindlib.util Desbordante.bindlib.data
)

# Uncomment to debug linker issues
#target_link_libraries(${MODULE_NAME} PRIVATE "-Wl,--no-undefined")

set(NAME bindlib.data)
desbordante_add_lib(NAME OBJECT)
target_sources(${NAME} PRIVATE data/bind_data_types.cpp)
target_link_libraries(${NAME} PRIVATE nanobind-static)

set(NAME bindlib.util)
desbordante_add_lib(NAME OBJECT)
target_sources(
    ${NAME}
    PRIVATE py_util/create_dataframe_reader.cpp
            py_util/dataframe_reader.cpp
            py_util/get_py_type.cpp
            py_util/iterable_sequence_stream.cpp
            py_util/logging.cpp
            py_util/opt_to_py.cpp
            py_util/py_custom_metrics.cpp
            py_util/py_to_any.cpp
            py_util/table_serialization.cpp
            py_util/value_to_py.cpp
)
target_link_libraries(
    ${NAME} PRIVATE spdlog::spdlog_header_only Boost::headers nanobind-static
                    ${DESBORDANTE_PREFIX}::config::enum_members_string
)

desbordante_add_bind(
    ac
    SRCS
    ac/bind_ac.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::ac
    magic_enum::magic_enum
    Boost::headers
)

desbordante_add_bind(
    afd_metric SRCS afd_metric/bind_afd_metric_calculation.cpp LIBS
    ${DESBORDANTE_PREFIX}::afd::metric_calc Boost::headers
)

desbordante_add_bind(
    ar
    SRCS
    ar/bind_ar.cpp
    ar/bind_ar_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::ar
    ${DESBORDANTE_PREFIX}::ar::apriori
    ${DESBORDANTE_PREFIX}::ar::verifier
    Boost::headers
)

desbordante_add_bind(
    cfd
    SRCS
    cfd/bind_cfd.cpp
    cfd/bind_cfd_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::cfd::fdfirst
    ${DESBORDANTE_PREFIX}::cfd::verifier
    Boost::headers
)

desbordante_add_bind(
    cind
    SRCS
    cind/bind_cind.cpp
    cind/bind_cind_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::cind
    ${DESBORDANTE_PREFIX}::cind::verifier
    Boost::headers
)

desbordante_add_bind(
    dc
    SRCS
    dc/bind_dc_verification.cpp
    dc/bind_fastadc.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::dc::verifier
    ${DESBORDANTE_PREFIX}::dc::fastadc
    ${DESBORDANTE_PREFIX}::bindlib::util
    spdlog::spdlog_header_only
    Boost::headers
)

desbordante_add_bind(
    dd
    SRCS
    dd/bind_dd_verification.cpp
    dd/bind_split.cpp
    dd/create_dd.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::dd::verifier
    ${DESBORDANTE_PREFIX}::dd::split
    ${DESBORDANTE_PREFIX}::dd
    Boost::headers
)

desbordante_add_bind(
    fd
    SRCS
    fd/bind_fd.cpp
    fd/bind_fd_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::fd::verifier
    ${DESBORDANTE_PREFIX}::fd::hy
    ${DESBORDANTE_PREFIX}::fd::aid
    ${DESBORDANTE_PREFIX}::fd::euler
    ${DESBORDANTE_PREFIX}::fd::depminer
    ${DESBORDANTE_PREFIX}::fd::dfd
    ${DESBORDANTE_PREFIX}::fd::fastfds
    ${DESBORDANTE_PREFIX}::fd::fdep
    ${DESBORDANTE_PREFIX}::fd::mine
    ${DESBORDANTE_PREFIX}::fd::fun
    ${DESBORDANTE_PREFIX}::fd::pyro
    ${DESBORDANTE_PREFIX}::bindlib::util
    magic_enum::magic_enum
    spdlog::spdlog_header_only
    Boost::headers
)

desbordante_add_bind(
    afd
    SRCS
    fd/bind_afd.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::fd::tane
    ${DESBORDANTE_PREFIX}::fd::tane::pfd
    ${DESBORDANTE_PREFIX}::bindlib::util
    spdlog::spdlog_header_only
    Boost::headers
)

desbordante_add_bind(
    fd.verifier.dynamic
    SRCS
    dynamic/bind_dynamic_fd_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::fd::verifier::dynamic
    magic_enum::magic_enum
    spdlog::spdlog_header_only
    Boost::headers
)

desbordante_add_bind(
    fem
    SRCS
    fem/bind_fem.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::fem::afem
    ${DESBORDANTE_PREFIX}::fem::maxfem
    ${DESBORDANTE_PREFIX}::fem::tke
    ${DESBORDANTE_PREFIX}::bindlib::util
    spdlog::spdlog_header_only
    Boost::headers
)

desbordante_add_bind(
    gdd
    SRCS
    gdd/bind_gdd_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::gdd
    ${DESBORDANTE_PREFIX}::gdd::validator
    Boost::headers
    spdlog::spdlog_header_only
)

desbordante_add_bind(
    gfd
    SRCS
    gfd/bind_gfd.cpp
    gfd/bind_gfd_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::gfd::miner
    ${DESBORDANTE_PREFIX}::gfd::validator
    Boost::headers
)

desbordante_add_bind(
    gspan SRCS gspan/bind_gspan.cpp LIBS ${DESBORDANTE_PREFIX}::fsm::gspan Boost::headers
)

desbordante_add_bind(
    ind
    SRCS
    ind/bind_ind.cpp
    ind/bind_ind_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::ind::verifier
    ${DESBORDANTE_PREFIX}::ind::spider
    ${DESBORDANTE_PREFIX}::ind::faida
    ${DESBORDANTE_PREFIX}::ind::mind
    Boost::headers
)

desbordante_add_bind(
    md
    SRCS
    md/bind_md.cpp
    md/bind_md_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::md::verifier
    ${DESBORDANTE_PREFIX}::bindlib::util
    magic_enum::magic_enum
    spdlog::spdlog_header_only
    Boost::headers
)

desbordante_add_bind(
    mfd
    SRCS
    mfd/bind_mfd_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::metric::verifier
    magic_enum::magic_enum
    Boost::headers
)

desbordante_add_bind(
    nar
    SRCS
    nar/bind_nar.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::nar::des
    magic_enum::magic_enum
    Boost::headers
)

desbordante_add_bind(
    nd
    SRCS
    nd/bind_nd.cpp
    nd/bind_nd_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::nd::verifier
    ${DESBORDANTE_PREFIX}::nd
    Boost::headers
)

desbordante_add_bind(
    od
    SRCS
    od/bind_od.cpp
    od/bind_od_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::od::fast
    ${DESBORDANTE_PREFIX}::od::order
    ${DESBORDANTE_PREFIX}::od::set_based_aod_verifier
    magic_enum::magic_enum
    spdlog::spdlog_header_only
    Boost::headers
)

desbordante_add_bind(
    pac
    SRCS
    pac/bind_domains.cpp
    pac/bind_pac.cpp
    pac/bind_pac_verification.cpp
    LIBS
    Desbordante::pac::verifier::domain_pac_verifier
    Desbordante::pac::verifier
    spdlog::spdlog_header_only
    Boost::headers
)

desbordante_add_bind(
    pfd SRCS pfd/bind_pfd_verification.cpp LIBS ${DESBORDANTE_PREFIX}::fd::pfd_verifier
    Boost::headers
)

desbordante_add_bind(
    sfd
    SRCS
    sfd/bind_sfd.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::fd::sfd::cords
    ${DESBORDANTE_PREFIX}::bindlib::util
    Boost::headers
)

desbordante_add_bind(
    statistics
    SRCS
    statistics/bind_statistics.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::datastats
    magic_enum::magic_enum
    Boost::headers
)

desbordante_add_bind(
    ucc
    SRCS
    ucc/bind_ucc.cpp
    ucc/bind_ucc_verification.cpp
    LIBS
    ${DESBORDANTE_PREFIX}::ucc::hpivalid
    ${DESBORDANTE_PREFIX}::ucc::hy
    ${DESBORDANTE_PREFIX}::ucc::pyro
    ${DESBORDANTE_PREFIX}::ucc::verifier
    ${DESBORDANTE_PREFIX}::bindlib::util
    Boost::headers
)

desbordante_add_bind(
    sd SRCS sd/bind_sd_verification.cpp LIBS ${DESBORDANTE_PREFIX}::sd::verifier Boost::headers
)

# ---- nanobind toolchain probe ----

# These modules verify the linked and Stable ABI modes of the nanobind toolchain. They are
# deliberately absent from install(TARGETS ...): only ${MODULE_NAME} is shipped in the wheel.

option(DESBORDANTE_BUILD_NANOBIND_ABI3_PROBE "Build the nanobind Stable ABI probe module" ON)

nanobind_add_module(_nb_probe nanobind_probe.cpp)
target_include_directories(_nb_probe PRIVATE ${DESBORDANTE_COMMON_INCLUDE_DIRS})
target_link_libraries(_nb_probe PRIVATE ${DESBORDANTE_PREFIX}.compile_feats)

if(DESBORDANTE_BUILD_NANOBIND_ABI3_PROBE
   AND TARGET Python::SABIModule
   AND Python_VERSION VERSION_GREATER_EQUAL 3.12
   AND Python_INTERPRETER_ID STREQUAL "Python"
   AND NOT NB_FREE_THREADED
)
    nanobind_add_module(_nb_probe_abi3 STABLE_ABI nanobind_probe.cpp)
    target_compile_definitions(_nb_probe_abi3 PRIVATE NANOBIND_PROBE_ABI3)
    target_include_directories(_nb_probe_abi3 PRIVATE ${DESBORDANTE_COMMON_INCLUDE_DIRS})
    target_link_libraries(_nb_probe_abi3 PRIVATE ${DESBORDANTE_PREFIX}.compile_feats)
else()
    message(
        STATUS
            "Not building _nb_probe_abi3: linked Stable ABI requires CPython >= 3.12 with Development.SABIModule"
    )
endif()

# ---- Installation Setup ----

if(DESBORDANTE_BINDINGS STREQUAL "INSTALL")
    # Install Python bindings module to system
    # Places the compiled library in the root installation directory
    install(TARGETS ${MODULE_NAME} LIBRARY DESTINATION .)
endif()
