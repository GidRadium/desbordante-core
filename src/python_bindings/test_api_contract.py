"""Contract tests for the public Python API of Desbordante.

These tests are the safety net for the pybind11 -> nanobind migration: they
freeze the public module/class inventory, algorithm option metadata, the
protocols implemented by pattern objects (``str``/``repr``/``==``/``hash``,
copy, pickle) and the exact pickle stream layout.

The tests use only the Python standard library (``unittest``); ``pandas`` is
imported lazily and the DataFrame checks are skipped when it is unavailable.

Golden pickle files
-------------------
``DESBORDANTE_RECORD_GOLDEN=1 python test_api_contract.py`` records the
reference streams in ``test_input_data/pickle_golden/<class>.pkl``.  A normal
run only reads them with the current build, checks their structure and
performs a pickle round-trip; it never rewrites them.

The goldens are produced from the following minimal datasets (paths are
resolved relative to the repository's ``test_input_data`` directory):

* ``WDC_satellites.csv`` - FD, AFD, UCC, CFD, OD (ascending, descending,
  simple);
* ``TestLong.csv`` - NAR, SFD, PAC (``DomainPAC``), custom metric input and
  the generic table input check;
* ``cind/cind_test_en.csv`` + ``cind/cind_test_de.csv`` - CIND conditions and
  the ``CINDVerifier`` cluster;
* ``animals_beverages.csv`` - MD (Levenshtein self-column matches);
* ``TestDD1.csv`` - DD;
* ``ind_data/two_tables/first.csv`` + ``second.csv`` - IND and
  ``ColumnCombination``;
* ``od_norm_data/small_2x3.csv`` - ``ListOD``;
* ``transactional_data/rules-kaggle-rows.csv`` - AR (``ARStrings``/``ArIDs``);
* ``TestDC1.csv`` - DC (FastADC);
* ``TestDataStats.csv`` - AC ranges/exceptions;
* ``graph_data/gspan_test_simple.txt`` - gspan ``EdgeList`` iterator lifetime.

Options used for the goldens are listed next to each fixture factory.  Data
set contents never enter the pickle streams as paths: the CSV parser stores
only the file's basename as the relation name.

The NAR golden keeps the baseline pybind11 stream untouched.  That stream
encodes the value-range ``TypeId`` (a ``char``) as a one-character string
while ``__setstate__`` expects an integer; the test applies a narrow,
documented compatibility shim when loading it so the same bytes can be
compared and re-serialized.
"""

import copy
import contextlib
import gc
import inspect
import json
import os
import pickle
import types
import unittest
from pathlib import Path

import desbordante as desb

try:
    import pandas
except ImportError:  # pragma: no cover - exercised only in minimal wheels
    pandas = None


_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent.parent
_INVENTORY_PATH = _HERE / "test_api_contract_inventory.json"
_GOLDEN_DIR = _HERE / "test_input_data" / "pickle_golden"
_RECORD_GOLDEN = os.environ.get("DESBORDANTE_RECORD_GOLDEN") == "1"

_REQUIRED_GOLDENS = (
    "FD",
    "UCC",
    "CFD",
    "Condition",
    "MD",
    "NAR",
    "DD",
    "IND",
    "AscCanonicalOD",
    "DescCanonicalOD",
    "SimpleCanonicalOD",
)

# The pickle state is the tuple returned by __getstate__.  Its arity is part
# of the on-disk format: old streams must keep loading after the migration.
_GOLDEN_STATE_ARITY = {
    "FD": 3,
    "AFD": 4,
    "UCC": 2,
    "CFD": 2,
    "Item": 2,
    "Condition": 3,
    "MD": 5,
    "NAR": 6,
    "DD": 2,
    "IND": 4,
    "AscCanonicalOD": 3,
    "DescCanonicalOD": 3,
    "SimpleCanonicalOD": 2,
    "ListOD": 2,
    "ColumnCombination": 2,
    "ARStrings": 4,
    "ArIDs": 4,
    "DC": 3,
    "ACRanges": 3,
    "ACException": 2,
    "DomainPAC": 5,
    "Correlation": 2,
    "ViolatingCluster": 2,
}

_GOLDEN_TYPES = {
    "FD": "desbordante.fd.FD",
    "AFD": "desbordante.afd.AFD",
    "UCC": "desbordante.ucc.UCC",
    "CFD": "desbordante.cfd.CFD",
    "Item": "desbordante.cfd.Item",
    "Condition": "desbordante.cind.Condition",
    "MD": "desbordante.md.MD",
    "NAR": "desbordante.nar.NAR",
    "DD": "desbordante.dd.DD",
    "IND": "desbordante.ind.IND",
    "AscCanonicalOD": "desbordante.od.AscCanonicalOD",
    "DescCanonicalOD": "desbordante.od.DescCanonicalOD",
    "SimpleCanonicalOD": "desbordante.od.SimpleCanonicalOD",
    "ListOD": "desbordante.od.ListOD",
    "ColumnCombination": "desbordante.data_types.ColumnCombination",
    "ARStrings": "desbordante.ar.ARStrings",
    "ArIDs": "desbordante.ar.ArIDs",
    "DC": "desbordante.dc.DC",
    "ACRanges": "desbordante.ac.ACRanges",
    "ACException": "desbordante.ac.ACException",
    "DomainPAC": "desbordante.pac.DomainPAC",
    "Correlation": "desbordante.sfd.Correlation",
    "ViolatingCluster": "desbordante.cind_verification.ViolatingCluster",
}

# Classes whose full object protocol (equality, hashing, copying and pickling)
# is frozen.  CIND itself is intentionally absent: bind_cind.cpp does not
# define py::pickle for it, only Condition is picklable.
_FULL_PROTOCOL_GOLDENS = (
    "FD",
    "AFD",
    "UCC",
    "CFD",
    "Item",
    "Condition",
    "MD",
    "NAR",
    "DD",
    "IND",
    "AscCanonicalOD",
    "DescCanonicalOD",
    "SimpleCanonicalOD",
    "DC",
    "DomainPAC",
    "Correlation",
)

# Classes that define a meaningful __str__ in the baseline.  Others use the
# default object representation, which contains an address and must not be
# compared between processes.
_CUSTOM_STR_GOLDENS = set(_FULL_PROTOCOL_GOLDENS) | {
    "ARStrings",
    "ColumnCombination",
}


def _dataset(relative_path):
    """Locate a test dataset both in a CI copy and in a source checkout."""
    relative = Path(relative_path)
    candidates = (
        Path.cwd() / relative,
        _HERE / relative,
        _HERE / relative.name,
        Path.cwd() / relative.name,
        _REPO_ROOT / "test_input_data" / relative,
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        f"Dataset {relative_path!r} was not found; searched: "
        + ", ".join(str(candidate) for candidate in candidates)
    )


def _resolve(path):
    value = desb
    for part in path.split(".")[1:]:
        value = getattr(value, part)
    return value


def _type_name(py_type):
    module = getattr(py_type, "__module__", None)
    qualname = getattr(py_type, "__qualname__", getattr(py_type, "__name__", None))
    if module and qualname:
        return f"{module}.{qualname}"
    return repr(py_type)


def _load_inventory():
    with _INVENTORY_PATH.open(encoding="utf-8") as file:
        return json.load(file)


def _collect_inventory(root):
    """Collect public submodules and classes reachable from *root*.

    Modules are identified by their canonical ``__name__`` so aliases such as
    ``desbordante.od_module`` do not produce duplicate entries; aliases are
    checked separately by the inventory test.
    """
    modules = set()
    classes = set()
    visited = set()

    def walk(module):
        if id(module) in visited:
            return
        visited.add(id(module))
        module_name = module.__name__
        modules.add(module_name)
        for name in dir(module):
            if name.startswith("_"):
                continue
            try:
                value = getattr(module, name)
            except Exception:  # pragma: no cover - defensive for exotic attrs
                continue
            if inspect.ismodule(value):
                walk(value)
            elif inspect.isclass(value):
                classes.add(f"{module_name}.{name}")

    walk(root)
    return sorted(modules), sorted(classes)


def _golden_path(name):
    return _GOLDEN_DIR / f"{name}.pkl"


def _write_golden(name, value):
    _GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
    with _golden_path(name).open("wb") as file:
        pickle.dump(value, file)


def _pickle_state(value):
    getstate = getattr(value, "__getstate__", None)
    if getstate is None:
        return None
    return getstate()


def _normalize_nar_state(state):
    """Work around a baseline NAR pickle bug.

    ``SerializeValueRange`` stores ``TypeId`` (a ``char`` enum) via
    ``py::make_tuple``; pybind11 converts ``char`` to a one-character string,
    while ``DeserializeValueRange`` casts it back to ``int``.  The raw golden
    bytes keep that baseline layout; this shim only makes them loadable.
    """
    if not isinstance(state, tuple) or len(state) != 6:
        return state

    def fix_range(pair):
        type_code, payload = pair
        if isinstance(type_code, str) and len(type_code) == 1:
            type_code = ord(type_code)
        return type_code, payload

    return (
        state[0],
        state[1],
        state[2],
        state[3],
        {key: fix_range(value) for key, value in state[4].items()},
        {key: fix_range(value) for key, value in state[5].items()},
    )


@contextlib.contextmanager
def _nar_setstate_compat():
    nar_class = desb.nar.NAR
    original_setstate = nar_class.__setstate__

    def __setstate__(self, state):
        return original_setstate(self, _normalize_nar_state(state))

    nar_class.__setstate__ = __setstate__
    try:
        yield
    finally:
        nar_class.__setstate__ = original_setstate


def _load_pickle(name, payload):
    if name == "NAR":
        with _nar_setstate_compat():
            return pickle.loads(payload)
    return pickle.loads(payload)


def _pickle_roundtrip(name, value):
    return _load_pickle(name, pickle.dumps(value))


def _copy_pattern(name, copier, value):
    if name == "NAR":
        with _nar_setstate_compat():
            return copier(value)
    return copier(value)


def _mine_fds(dataset):
    algorithm = desb.fd.algorithms.HyFD()
    algorithm.load_data(table=(str(dataset), ",", True))
    algorithm.execute()
    return algorithm.get_fds()


def _mine_afds(dataset):
    algorithm = desb.afd.algorithms.Tane()
    algorithm.load_data(table=(str(dataset), ",", True))
    algorithm.execute(error=0.01)
    return algorithm.get_fds()


def _mine_uccs(dataset):
    algorithm = desb.ucc.algorithms.HyUCC()
    algorithm.load_data(table=(str(dataset), ",", True))
    algorithm.execute()
    return algorithm.get_uccs()


def _mine_cfds(dataset):
    algorithm = desb.cfd.algorithms.FDFirst()
    algorithm.load_data(table=(str(dataset), ",", True))
    algorithm.execute(cfd_minsup=1, cfd_minconf=0.5, cfd_max_lhs=2)
    return algorithm.get_cfds()


def _mine_cinds():
    # CIND keeps a reference to the IND owned by the algorithm, so the
    # algorithm must outlive the returned patterns.
    algorithm = desb.cind.algorithms.Cinderella()
    algorithm.load_data(
        tables=[
            (str(_dataset("cind/cind_test_en.csv")), ",", True),
            (str(_dataset("cind/cind_test_de.csv")), ",", True),
        ]
    )
    algorithm.execute(
        error=0.0, validity=0.0, completeness=0.01,
        condition_type="row", algo_type="cinderella",
    )
    return algorithm, algorithm.get_cinds()


def _mine_mds():
    table = _dataset("animals_beverages.csv")
    algorithm = desb.md.algorithms.HyMD()
    algorithm.load_data(left_table=(str(table), ",", True))
    column_matches = [
        desb.md.column_matches.Levenshtein(index, index) for index in range(4)
    ]
    algorithm.execute(column_matches=column_matches)
    return algorithm.get_mds()


def _mine_nars():
    algorithm = desb.nar.algorithms.DES()
    algorithm.load_data(table=(str(_dataset("TestLong.csv")), ",", True))
    algorithm.execute(
        minsup=0.0,
        minconf=0.0,
        population_size=20,
        max_fitness_evaluations=20,
        seed=1,
        differential_strategy="rand1_bin",
    )
    return algorithm.get_nars()


def _mine_dds():
    algorithm = desb.dd.algorithms.Split()
    algorithm.load_data(table=(str(_dataset("TestDD1.csv")), ",", True))
    algorithm.execute()
    return algorithm.get_dds()


def _mine_inds_and_column_combinations():
    algorithm = desb.ind.algorithms.Spider()
    algorithm.load_data(
        tables=[
            (str(_dataset("ind_data/two_tables/first.csv")), ",", False),
            (str(_dataset("ind_data/two_tables/second.csv")), ",", False),
        ]
    )
    algorithm.execute()
    inds = algorithm.get_inds()
    combinations = []
    for ind in inds:
        combinations.extend((ind.get_lhs(), ind.get_rhs()))
    return inds, combinations


def _mine_ods():
    algorithm = desb.od.algorithms.Fastod()
    algorithm.load_data(table=(str(_dataset("WDC_satellites.csv")), ",", True))
    algorithm.execute(time_limit=1)
    return {
        "AscCanonicalOD": algorithm.get_asc_ods(),
        "DescCanonicalOD": algorithm.get_desc_ods(),
        "SimpleCanonicalOD": algorithm.get_simple_ods(),
    }


def _mine_list_ods():
    algorithm = desb.od.algorithms.Order()
    algorithm.load_data(table=(str(_dataset("od_norm_data/small_2x3.csv")), ",", True))
    algorithm.execute()
    return algorithm.get_list_ods()


def _mine_ars():
    algorithm = desb.ar.algorithms.Apriori()
    algorithm.load_data(
        table=(str(_dataset("transactional_data/rules-kaggle-rows.csv")), ",", True),
        input_format="tabular",
        has_tid=True,
    )
    algorithm.execute(minsup=0.1, minconf=0.1)
    return algorithm.get_ars(), algorithm.get_ar_ids()


def _mine_dcs():
    algorithm = desb.dc.algorithms.FastADC()
    algorithm.load_data(table=(str(_dataset("TestDC1.csv")), ",", True))
    algorithm.execute(shard_length=10)
    # DC keeps pointers into the algorithm's predicate provider/schema.
    return algorithm, algorithm.get_dcs()


def _mine_ac():
    algorithm = desb.ac.algorithms.AcAlgorithm()
    algorithm.load_data(table=(str(_dataset("TestDataStats.csv")), ",", True))
    algorithm.execute(
        p_fuzz=0.85,
        fuzziness=0.2,
        bumps_limit=0,
        weight=0.1,
        bin_operation="+",
        ac_seed=11,
        iterations_limit=4,
    )
    return algorithm.get_ac_ranges(), algorithm.get_ac_exceptions()


def _mine_pac():
    algorithm = desb.pac_verification.algorithms.DomainPACVerifier()
    algorithm.load_data(
        table=(str(_dataset("TestLong.csv")), ",", True),
        column_indices=[0, 1],
        domain=desb.pac.domains.Parallelepiped(["0", "0"], ["5", "5"], [1, 1.2]),
    )
    algorithm.execute(min_epsilon=0, max_epsilon=7, min_delta=0.8, diagonal_threshold=1e-10)
    return [algorithm.get_pac()]


def _mine_correlations():
    algorithm = desb.sfd.algorithms.SFDAlgorithm()
    algorithm.load_data(table=(str(_dataset("TestLong.csv")), ",", True))
    algorithm.execute(
        min_sfd_strength=0.1,
        delta=0.11,
        max_false_positive_probability=1e-6,
        only_sfd=False,
        min_cardinality=0.1,
        max_amount_of_categories=100,
        min_skew_threshold=0.5,
        min_structural_zeroes_amount=0.3,
        max_different_values_proportion=0.99,
    )
    # Correlation keeps Column objects pointing into the algorithm's schema,
    # so the algorithm has to stay alive while the golden is serialized.
    return algorithm, algorithm.get_correlations()


def _mine_cind_violating_clusters():
    verifier = desb.cind_verification.algorithms.CINDVerifier()
    verifier.load_data(
        tables=[
            (str(_dataset("cind/cind_test_en.csv")), ",", True),
            (str(_dataset("cind/cind_test_de.csv")), ",", True),
        ],
    )
    verifier.execute(lhs_indices=[0], rhs_indices=[0], error=0.0)
    return verifier.get_violating_clusters()


def _record_goldens():
    """Create the reference pickle streams from the current (pybind11) build."""
    fd_source = _dataset("WDC_satellites.csv")
    fds = _mine_fds(fd_source)
    if not fds:
        fds = _mine_fds(_dataset("TestLong.csv"))
    if not fds:
        raise RuntimeError("Could not mine an FD for the golden pickle")

    afds = _mine_afds(_dataset("WDC_satellites.csv"))
    if not afds:
        afds = _mine_afds(_dataset("TestLong.csv"))
    if not afds:
        raise RuntimeError("Could not mine an AFD for the golden pickle")

    uccs = _mine_uccs(fd_source)
    if not uccs:
        uccs = _mine_uccs(_dataset("TestLong.csv"))
    if not uccs:
        raise RuntimeError("Could not mine a UCC for the golden pickle")

    cfds = _mine_cfds(_dataset("WDC_satellites.csv"))
    if not cfds:
        cfds = _mine_cfds(_dataset("TestLong.csv"))
    if not cfds:
        raise RuntimeError("Could not mine a CFD for the golden pickle")

    cind_algorithm, cinds = _mine_cinds()
    conditions = [condition for cind in cinds for condition in cind.get_conditions()]
    if not conditions:
        raise RuntimeError("Could not mine a CIND condition for the golden pickle")

    mds = _mine_mds()
    if not mds:
        raise RuntimeError("Could not mine an MD for the golden pickle")

    nars = _mine_nars()
    if not nars:
        raise RuntimeError("Could not mine a NAR for the golden pickle")

    dds = _mine_dds()
    if not dds:
        raise RuntimeError("Could not mine a DD for the golden pickle")

    inds, combinations = _mine_inds_and_column_combinations()
    if not inds or not combinations:
        raise RuntimeError("Could not mine an IND for the golden pickle")

    ods = _mine_ods()
    for name, values in ods.items():
        if not values:
            raise RuntimeError(f"Could not mine {name} for the golden pickle")
    list_ods = _mine_list_ods()
    if not list_ods:
        raise RuntimeError("Could not mine a ListOD for the golden pickle")

    ars, ar_ids = _mine_ars()
    if not ars or not ar_ids:
        raise RuntimeError("Could not mine an association rule for the golden pickle")

    dc_algorithm, dcs = _mine_dcs()
    if not dcs:
        raise RuntimeError("Could not mine a DC for the golden pickle")

    ac_ranges, ac_exceptions = _mine_ac()
    if not ac_ranges:
        raise RuntimeError("Could not mine AC ranges for the golden pickle")
    if not ac_exceptions:
        raise RuntimeError("Could not mine an AC exception for the golden pickle")

    pacs = _mine_pac()
    if not pacs:
        raise RuntimeError("Could not mine a DomainPAC for the golden pickle")

    sfd_algorithm, correlations = _mine_correlations()
    if not correlations:
        raise RuntimeError("Could not mine a correlation for the golden pickle")

    violating_clusters = _mine_cind_violating_clusters()
    if not violating_clusters:
        raise RuntimeError("Could not mine a violating cluster for the golden pickle")

    values = {
        "FD": fds[0],
        "AFD": afds[0],
        "UCC": uccs[0],
        "CFD": cfds[0],
        "Item": desb.cfd.Item(0, None),
        "Condition": conditions[0],
        "MD": mds[0],
        "NAR": nars[0],
        "DD": dds[0],
        "IND": inds[0],
        "AscCanonicalOD": ods["AscCanonicalOD"][0],
        "DescCanonicalOD": ods["DescCanonicalOD"][0],
        "SimpleCanonicalOD": ods["SimpleCanonicalOD"][0],
        "ListOD": list_ods[0],
        "ColumnCombination": combinations[0],
        "ARStrings": ars[0],
        "ArIDs": ar_ids[0],
        "DC": dcs[0],
        "ACRanges": ac_ranges[0],
        "ACException": ac_exceptions[0],
        "DomainPAC": pacs[0],
        "Correlation": correlations[0],
        "ViolatingCluster": violating_clusters[0],
    }
    for name, value in values.items():
        _write_golden(name, value)


def setUpModule():
    if _RECORD_GOLDEN:
        _record_goldens()


class TestPublicInventory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.expected = _load_inventory()

    def test_submodules(self):
        actual_modules, _ = _collect_inventory(desb)
        self.assertEqual(actual_modules, self.expected["modules"])

    def test_classes(self):
        _, actual_classes = _collect_inventory(desb)
        self.assertEqual(actual_classes, self.expected["classes"])

    def test_required_aliases(self):
        self.assertIs(desb.od_module, desb.od)
        self.assertIs(desb.afd_verification, desb.fd_verification)
        self.assertIs(desb.aind_verification, desb.ind_verification)
        self.assertTrue(inspect.ismodule(desb.aod_verification))
        self.assertIs(desb.aucc_verification, desb.ucc_verification)
        self.assertIs(
            desb.afd.algorithms.Pyro,
            desb.fd.algorithms.Pyro,
        )
        for name in ("Pyro",):
            self.assertIn(name, dir(desb.fd.algorithms))
            self.assertIn(name, dir(desb.afd.algorithms))
        self.assertIs(desb.ind.algorithms.Spider, desb.aind.algorithms.Spider)
        self.assertIs(desb.ind.algorithms.Mind, desb.aind.algorithms.Mind)
        self.assertIs(
            desb.ucc_verification.algorithms.UccVerifier,
            desb.aucc_verification.algorithms.UccVerifier,
        )

    def test_fem_exposes_algorithms_directly(self):
        for name in ("MaxFEM", "AFEM", "TKE"):
            self.assertTrue(inspect.isclass(getattr(desb.fem, name)))
        self.assertTrue(hasattr(desb.fem, "FEMAlgorithm"))


class TestAlgorithmOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.expected = _load_inventory()["algorithms"]

    def _check_algorithm(self, path, expected):
        with self.subTest(algorithm=path):
            algorithm_class = _resolve(path)
            self.assertTrue(inspect.isclass(algorithm_class))
            self.assertTrue(issubclass(algorithm_class, desb.Algorithm))

            algorithm = algorithm_class()
            self.assertEqual(
                sorted(algorithm._get_needed_options()), expected["needed_options"]
            )
            self.assertEqual(
                sorted(algorithm.get_possible_options()), expected["possible_options"]
            )

            for option_name, expected_types in expected["option_types"].items():
                actual_types = algorithm._get_option_type(option_name)
                self.assertIsInstance(actual_types, tuple)
                self.assertEqual(
                    [_type_name(py_type) for py_type in actual_types],
                    expected_types,
                    msg=f"{path}._get_option_type({option_name!r})",
                )

            for option_name in expected["unsupported_option_types"]:
                with self.assertRaises(RuntimeError):
                    algorithm._get_option_type(option_name)

            table_options = set(expected["table_options"])
            unsupported_options = set(expected["unsupported_option_types"])
            default_options = set(expected["default_options"])
            non_default_options = set(expected["non_default_options"])
            for option_name in expected["possible_options"]:
                if option_name in table_options or option_name in unsupported_options:
                    continue
                if option_name in default_options:
                    algorithm._set_option(option_name, None)
                else:
                    self.assertIn(option_name, non_default_options)
                    with self.assertRaises(desb.ConfigurationError):
                        algorithm._set_option(option_name, None)

            self.assertEqual(
                sorted(algorithm._get_needed_options()),
                expected["needed_after_defaults"],
            )
            if expected["opts_error"] is not None:
                with self.assertRaises(RuntimeError):
                    algorithm.get_opts()
            else:
                opts = algorithm.get_opts()
                self.assertIsInstance(opts, dict)
                self.assertEqual(sorted(opts), expected["opts_keys"])
                self.assertNotIn("table", opts)
                self.assertNotIn("sequence", opts)

    def test_algorithms(self):
        for path, expected in self.expected.items():
            self._check_algorithm(path, expected)

    def test_table_option_type(self):
        for path, expected in self.expected.items():
            with self.subTest(algorithm=path):
                algorithm = _resolve(path)()
                for option_name in expected["table_options"]:
                    self.assertIn(
                        "desbordante.data_types.Table",
                        [_type_name(py_type) for py_type in algorithm._get_option_type(option_name)],
                    )

    def test_unknown_option_type_raises_configuration_error(self):
        algorithm = desb.fd.algorithms.HyFD()
        with self.assertRaises(desb.ConfigurationError):
            algorithm._get_option_type("this_option_does_not_exist")


class TestConfigurationError(unittest.TestCase):
    def test_is_value_error_subclass(self):
        self.assertTrue(issubclass(desb.ConfigurationError, ValueError))


class TestEnums(unittest.TestCase):
    def test_distance_metric(self):
        expected = {"ABS_DIFF": 0, "EDIT_DISTANCE": 1}
        for name, value in expected.items():
            member = getattr(desb.gdd.DistanceMetric, name)
            self.assertEqual(member.value, value)
            self.assertEqual(member.name, name)
            self.assertIs(getattr(desb.gdd, name), member)
            self.assertEqual(repr(member), f"<DistanceMetric.{name}: {value}>")

    def test_cmp_op(self):
        expected = {"LE": 0, "GE": 1, "LT": 2, "GT": 3, "EQ": 4, "NE": 5}
        for name, value in expected.items():
            member = getattr(desb.gdd.CmpOp, name)
            self.assertEqual(member.value, value)
            self.assertEqual(member.name, name)
            self.assertIs(getattr(desb.gdd, name), member)
            self.assertEqual(repr(member), f"<CmpOp.{name}: {value}>")


class TestPatternProtocols(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.objects = {
            name: _load_pickle(name, _golden_path(name).read_bytes())
            for name in _FULL_PROTOCOL_GOLDENS
        }

    def test_str_and_repr(self):
        for name, value in self.objects.items():
            with self.subTest(pattern=name):
                self.assertIsInstance(str(value), str)
                self.assertTrue(str(value))
                self.assertIsInstance(repr(value), str)
                self.assertTrue(repr(value))

    def test_equality_hash_and_set(self):
        for name, value in self.objects.items():
            with self.subTest(pattern=name):
                clone = _pickle_roundtrip(name, value)
                self.assertEqual(value, clone)
                self.assertEqual(hash(value), hash(clone))
                self.assertEqual(len({value, clone}), 1)
                self.assertEqual(len({value, value}), 1)

    def test_copy(self):
        for name, value in self.objects.items():
            for copier in (copy.copy, copy.deepcopy):
                with self.subTest(pattern=name, copier=copier.__name__):
                    clone = _copy_pattern(name, copier, value)
                    self.assertIsNot(clone, value)
                    self.assertEqual(value, clone)
                    self.assertEqual(hash(value), hash(clone))

    def test_pickle_roundtrip(self):
        for name, value in self.objects.items():
            with self.subTest(pattern=name):
                clone = _pickle_roundtrip(name, value)
                self.assertIs(type(clone), type(value))
                self.assertEqual(
                    pickle.dumps(_pickle_state(value)),
                    pickle.dumps(_pickle_state(clone)),
                )
                self.assertEqual(str(value), str(clone))
                self.assertEqual(value, clone)

    def test_cind_is_hashable_and_usable_in_set(self):
        _algorithm, cinds = _mine_cinds()
        self.assertTrue(cinds)
        value = cinds[0]
        self.assertIsInstance(str(value), str)
        self.assertTrue(str(value))
        self.assertIsInstance(repr(value), str)
        self.assertEqual(value, value)
        self.assertEqual(hash(value), hash(value))
        self.assertEqual(len({value, value}), 1)

    def test_cind_copy_and_pickle_are_not_supported(self):
        # bind_cind.cpp defines py::pickle only for Condition, not for CIND
        # itself.  Keep that baseline limitation explicit: Condition carries
        # the complete pickle state and is covered by the golden tests.
        _algorithm, cinds = _mine_cinds()
        value = cinds[0]
        for copier in (copy.copy, copy.deepcopy):
            with self.assertRaises(TypeError):
                copier(value)
        with self.assertRaises(TypeError):
            pickle.dumps(value)


class TestPickleGoldens(unittest.TestCase):
    def test_required_goldens_exist(self):
        missing = [name for name in _REQUIRED_GOLDENS if not _golden_path(name).is_file()]
        self.assertEqual(missing, [])

    def test_goldens_load_and_roundtrip(self):
        names = sorted(_GOLDEN_TYPES)
        for name in names:
            with self.subTest(pattern=name):
                path = _golden_path(name)
                self.assertTrue(path.is_file(), msg=f"missing golden {path}")
                raw = path.read_bytes()
                value = _load_pickle(name, raw)
                expected_type = _resolve(_GOLDEN_TYPES[name])
                self.assertIsInstance(value, expected_type)

                state = _pickle_state(value)
                self.assertIsInstance(state, tuple)
                self.assertEqual(len(state), _GOLDEN_STATE_ARITY[name])

                clone = _pickle_roundtrip(name, value)
                self.assertIs(type(clone), type(value))
                self.assertEqual(
                    pickle.dumps(_pickle_state(clone)),
                    pickle.dumps(state),
                )
                if name in _CUSTOM_STR_GOLDENS:
                    self.assertEqual(str(clone), str(value))

                if name in _FULL_PROTOCOL_GOLDENS:
                    self.assertEqual(clone, value)
                    self.assertEqual(hash(clone), hash(value))


class TestInputKinds(unittest.TestCase):
    def test_table_tuple(self):
        algorithm = desb.fd.algorithms.HyFD()
        algorithm.load_data(table=(str(_dataset("TestLong.csv")), ",", True))
        algorithm.execute()
        self.assertTrue(algorithm.get_fds())

    @unittest.skipUnless(pandas is not None, "pandas is not installed")
    def test_pandas_dataframe(self):
        dataframe = pandas.DataFrame({"a": [1, 2, 3, 4], "b": [1, 1, 2, 2]})
        algorithm = desb.fd.algorithms.HyFD()
        algorithm.load_data(table=dataframe)
        algorithm.execute()
        self.assertTrue(algorithm.get_fds())

    def test_fem_iterable_sequence(self):
        algorithm = desb.fem.MaxFEM()
        algorithm.load_data(sequence=[[1], [2], [1], [2], [1]])
        algorithm.execute(minsup=3, window_size=1)
        self.assertEqual(algorithm.get_max_frequent_episodes(), [([[1]], 3)])

    def test_gspan_iterator_survives_source_deletion(self):
        algorithm = desb.gspan.algorithms.GSpan()
        algorithm.load_data(
            graph_database=str(_dataset("graph_data/gspan_test_simple.txt")),
            minsup=0.6,
            output_single_vertices=False,
        )
        algorithm.execute()
        subgraphs = algorithm.get_frequent_subgraphs()
        self.assertTrue(subgraphs)
        edge_list = subgraphs[0].edge_list
        expected = [repr(edge) for edge in edge_list]
        iterator = iter(edge_list)
        del edge_list, subgraphs, algorithm
        gc.collect()
        self.assertEqual([repr(edge) for edge in iterator], expected)

    def test_custom_metric_if_supported(self):
        algorithm = desb.mfd_verification.algorithms.MetricVerifier()
        metric_types = algorithm._get_option_type("metric")
        if types.FunctionType not in metric_types:
            self.skipTest("custom Python metrics are not supported by this build")
        algorithm.load_data(table=(str(_dataset("TestLong.csv")), ",", True))
        algorithm.execute(
            lhs_indices=[0],
            rhs_indices=[1],
            metric=lambda left, right: abs(float(left) - float(right)),
            metric_algorithm="brute",
            parameter=1.0,
        )
        self.assertIsInstance(algorithm.mfd_holds(), bool)


if __name__ == "__main__":
    unittest.main()
