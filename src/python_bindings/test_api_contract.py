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

Every class with a native ``__getstate__``/``__setstate__`` pair is covered
(23 classes in 17 binding translation units):

* ``fd/bind_fd.cpp`` - ``FD``; ``fd/bind_afd.cpp`` - ``AFD``;
* ``ucc/bind_ucc.cpp`` - ``UCC``; ``cfd/bind_cfd.cpp`` - ``Item``, ``CFD``;
* ``cind/bind_cind.cpp`` - ``Condition``;
  ``cind/bind_cind_verification.cpp`` - ``ViolatingCluster``;
* ``md/bind_md.cpp`` - ``MD``; ``nar/bind_nar.cpp`` - ``NAR``;
* ``dd/bind_split.cpp`` - ``DD``; ``ind/bind_ind.cpp`` - ``IND``;
* ``data/bind_data_types.cpp`` - ``ColumnCombination``;
* ``od/bind_od.cpp`` - ``AscCanonicalOD``, ``DescCanonicalOD``,
  ``SimpleCanonicalOD``, ``ListOD``;
* ``ar/bind_ar.cpp`` - ``ARStrings``, ``ArIDs``;
* ``dc/bind_fastadc.cpp`` - ``DC``; ``ac/bind_ac.cpp`` - ``ACRanges``,
  ``ACException``;
* ``pac/bind_pac.cpp`` - ``DomainPAC``; ``sfd/bind_sfd.cpp`` - ``Correlation``.

Every ``__setstate__`` first validates the tuple arity and all nested fields it
indexes, and only then placement-news the object into the storage supplied by
Python's pickle machinery.  Nothing is assigned to a live instance and no
destructor is called; malformed states raise before the placement new.

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

The state shape is frozen against the baseline by decoding the raw golden
streams with a private unpickler that substitutes a stub class and therefore
does not call into the current bindings at all.  NAR stores the value-range
``TypeId`` (a ``char`` enum) as a one-character string, exactly as pybind11
did; ``__setstate__`` accepts both that layout and an integer type code, so
the baseline stream loads natively and both enum conversions stay readable.
"""

import contextlib
import copy
import enum
import gc
import inspect
import io
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
_GOLDEN_PICKLE_PROTOCOL = 4

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
# define py::pickle for it, only Condition is picklable.  Picklable classes
# without a baseline __eq__/__hash__ (ListOD, ColumnCombination, the AR and AC
# patterns, ViolatingCluster) are still covered by TestPickleCopy and the
# golden state checks.
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
        pickle.dump(value, file, protocol=_GOLDEN_PICKLE_PROTOCOL)


def _pickle_state(value):
    getstate = getattr(value, "__getstate__", None)
    if getstate is None:
        return None
    return getstate()


class _GoldenStateStub:
    """Stand-in for the pickled class used to decode a baseline state.

    Substitute every global referenced by the stream with this stub.  Besides
    the top-level pattern class, CFD and IND reference small value classes;
    all of them are replaced here.  The states consist of built-in containers
    and scalars, so no binding code runs and the baseline tuple/list/dict
    layout can be inspected directly.
    """

    def __setstate__(self, state):
        self.state = state


class _GoldenStateUnpickler(pickle.Unpickler):
    def find_class(self, module, name):
        return _GoldenStateStub


def _load_golden_state(name):
    with _golden_path(name).open("rb") as file:
        return _GoldenStateUnpickler(file).load().state


def _state_signature(value):
    """Return a recursively typed, order-insensitive view of a pickle state."""
    if isinstance(value, tuple):
        return ("tuple", tuple(_state_signature(item) for item in value))
    if isinstance(value, list):
        return ("list", tuple(_state_signature(item) for item in value))
    if isinstance(value, dict):
        items = [
            (type(key).__name__, repr(key), _state_signature(key), _state_signature(item))
            for key, item in value.items()
        ]
        items.sort()
        return ("dict", tuple(items))
    if isinstance(value, _GoldenStateStub):
        return ("object", _state_signature(value.state))
    if not isinstance(value, (str, bytes, int, float, bool, type(None))):
        getstate = getattr(value, "__getstate__", None)
        if getstate is not None:
            return ("object", _state_signature(getstate()))
    return (type(value).__name__, value)


def _load_pickle(payload):
    return pickle.loads(payload)


def _pickle_roundtrip(value):
    return _load_pickle(pickle.dumps(value))


def _copy_pattern(copier, value):
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

    def test_option_type_names_match_baseline(self):
        """Every option's ``_get_option_type`` tuple matches the T01 inventory.

        This is the explicit T05 check for the Python type names exposed by the
        migration: ``int``/``float``/``str``/``list[...]`` and
        ``data_types.Table`` must not degrade to C++ names.
        """
        checked = 0
        for path, expected in self.expected.items():
            with self.subTest(algorithm=path):
                algorithm = _resolve(path)()
                for option_name, expected_types in expected["option_types"].items():
                    actual_types = algorithm._get_option_type(option_name)
                    self.assertEqual(
                        [_type_name(py_type) for py_type in actual_types],
                        expected_types,
                        msg=f"{path}._get_option_type({option_name!r})",
                    )
                    checked += 1
        self.assertGreater(checked, 0)

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

    def test_are_python_enum_classes(self):
        expected = {
            "DistanceMetric": {"ABS_DIFF": 0, "EDIT_DISTANCE": 1},
            "CmpOp": {"LE": 0, "GE": 1, "LT": 2, "GT": 3, "EQ": 4, "NE": 5},
        }
        for class_name, members in expected.items():
            with self.subTest(enum=class_name):
                enum_class = getattr(desb.gdd, class_name)
                self.assertTrue(issubclass(enum_class, enum.Enum))
                self.assertFalse(issubclass(enum_class, enum.IntEnum))
                self.assertEqual(
                    {name: member.value for name, member in enum_class.__members__.items()},
                    members,
                )
                for name, value in members.items():
                    member = enum_class[name]
                    self.assertIs(enum_class(value), member)
                    self.assertIs(getattr(enum_class, name), member)
                    self.assertIsInstance(member, enum_class)
                    self.assertIsInstance(member, enum.Enum)
                    self.assertNotIsInstance(member, int)
                    self.assertIsInstance(member.value, int)
                    # Unlike an IntEnum, an enum member is not equal to its raw
                    # integer value (pybind11 behaved the same way).
                    self.assertFalse(member == value)
                    self.assertEqual(hash(member), hash(member))

    def test_int_conversion_is_not_implicit(self):
        for member in (*desb.gdd.DistanceMetric, *desb.gdd.CmpOp):
            with self.subTest(member=member):
                with self.assertRaises(TypeError):
                    int(member)
                self.assertEqual(int(member.value), member.value)


class TestNoneSemantics(unittest.TestCase):
    def test_set_option_none_resets_default(self):
        algorithm = desb.fd.algorithms.EulerFD()
        algorithm._set_option("is_null_equal_null", False)
        self.assertIs(algorithm.get_opts()["is_null_equal_null"], False)
        algorithm._set_option("is_null_equal_null", None)
        self.assertIs(algorithm.get_opts()["is_null_equal_null"], True)

    def test_set_option_omitted_value_uses_none_default(self):
        algorithm = desb.fd.algorithms.EulerFD()
        algorithm._set_option("is_null_equal_null", False)
        # ``option_value`` defaults to ``nb::none()``.
        algorithm._set_option("is_null_equal_null")
        self.assertIs(algorithm.get_opts()["is_null_equal_null"], True)

    def test_optional_parameter_accepts_none(self):
        item = desb.cfd.Item(0, None)
        self.assertEqual(item.attribute, 0)
        self.assertIsNone(item.value)

        # A std::optional<T> argument accepts None without an explicit
        # ``.none()`` annotation; the nested tuple constructor follows suit.
        cfd = desb.cfd.CFD([(0, None)], (1, None))
        self.assertIsNone(cfd.rhs.value)
        self.assertEqual(list(cfd.lhs), [item])

    def test_callable_arguments_accept_none_only_where_declared(self):
        levenshtein = desb.md.column_matches.Levenshtein(0, 0, column_functions=None)
        self.assertIsInstance(levenshtein, desb.md.column_matches.Levenshtein)

        custom = desb.md.column_matches.Custom(
            lambda left, right: 1.0, 0, 0, column_functions=None
        )
        self.assertIsInstance(custom, desb.md.column_matches.Custom)

        # ``pick_lhs_indices`` is a typed callable without a None default.
        with self.assertRaises(TypeError):
            desb.md.column_matches.Levenshtein(0, 0, pick_lhs_indices=None)


class TestBoolSemantics(unittest.TestCase):
    BOOL_OPTION_CASES = (
        ("desbordante.fd.algorithms.EulerFD", "is_null_equal_null"),
        ("desbordante.ind.algorithms.Spider", "is_null_equal_null"),
        ("desbordante.statistics.algorithms.DataStats", "is_null_equal_null"),
        ("desbordante.fd_verification.algorithms.FDVerifier", "is_null_equal_null"),
        ("desbordante.nd_verification.algorithms.NDVerifier", "is_null_equal_null"),
    )

    @classmethod
    def setUpClass(cls):
        cls.dataset = str(_dataset("WDC_satellites.csv"))

    def test_set_option_bool_is_strict(self):
        for path, option_name in self.BOOL_OPTION_CASES:
            algorithm = _resolve(path)()
            with self.subTest(algorithm=path, option=option_name):
                self.assertEqual(algorithm._get_option_type(option_name), (bool,))
                for value in (True, False):
                    algorithm._set_option(option_name, value)
                    self.assertIs(algorithm.get_opts()[option_name], value)
                # ``None`` means "reset to the default" for _set_option.
                algorithm._set_option(option_name, False)
                algorithm._set_option(option_name, None)
                self.assertIs(algorithm.get_opts()[option_name], True)
                for invalid in (1, 0, "true", [], object()):
                    with self.assertRaises(desb.ConfigurationError):
                        algorithm._set_option(option_name, invalid)

    def test_load_data_bool_is_strict(self):
        for value in (True, False):
            algorithm = desb.fd.algorithms.EulerFD()
            algorithm.load_data(
                table=(self.dataset, ",", True), is_null_equal_null=value
            )
            self.assertIs(algorithm.get_opts()["is_null_equal_null"], value)

        for invalid in (1, 0, None, "true", []):
            algorithm = desb.fd.algorithms.EulerFD()
            with self.assertRaises(desb.ConfigurationError):
                algorithm.load_data(
                    table=(self.dataset, ",", True), is_null_equal_null=invalid
                )

    def test_execute_bool_is_strict(self):
        # Bool options are consumed by load_data in these algorithms, so
        # execute() is probed on a fresh instance where the option is still
        # needed and therefore converted.
        for invalid in (1, 0, None, "true", []):
            with self.assertRaises(desb.ConfigurationError) as raised:
                desb.fd.algorithms.EulerFD().execute(is_null_equal_null=invalid)
            self.assertIn("Unable to cast", str(raised.exception))

        for value in (True, False):
            with self.assertRaises(desb.ConfigurationError) as raised:
                desb.fd.algorithms.EulerFD().execute(is_null_equal_null=value)
            self.assertNotIn("Unable to cast", str(raised.exception))

    def test_table_tuple_flag_is_strict(self):
        for value in (True, False):
            algorithm = desb.fd.algorithms.EulerFD()
            algorithm.load_data(table=(self.dataset, ",", value))
            self.assertIs(algorithm.get_opts()["is_null_equal_null"], True)

        for invalid in (1, 0, None, "true"):
            algorithm = desb.fd.algorithms.EulerFD()
            with self.assertRaises(desb.ConfigurationError):
                algorithm.load_data(table=(self.dataset, ",", invalid))


class TestMdConstructors(unittest.TestCase):
    COLUMN_MATCHES = (
        "Levenshtein",
        "MongeElkan",
        "Jaccard",
        "Lcs",
        "LVNormDateDistance",
        "LVNormNumberDistance",
    )

    def test_defaults_and_keyword_only_arguments(self):
        for name in self.COLUMN_MATCHES:
            cls = getattr(desb.md.column_matches, name)
            with self.subTest(column_match=name):
                cls(0, 0)
                cls(0, 0, 0.5)
                cls(left_column=0, right_column=1, minimum_similarity=0.5)
                cls(0, 0, 0.5, bound_number_limit=2)
                cls(0, 0, 0.5, column_functions=None)
                with self.assertRaises(TypeError):
                    cls(0)
                with self.assertRaises(TypeError):
                    cls(0, 0, 0.5, 2)  # bound_number_limit is keyword-only

    def test_column_functions_accept_none_callable_and_pair(self):
        for name in self.COLUMN_MATCHES:
            cls = getattr(desb.md.column_matches, name)
            with self.subTest(column_match=name):
                cls(0, 0, column_functions=None)
                cls(0, 0, column_functions=lambda value: value)
                cls(0, 0, column_functions=(lambda value: value, lambda value: value))
                with self.assertRaises(TypeError):
                    cls(0, 0, column_functions=1)

    def test_pick_lhs_indices_requires_a_callable(self):
        for name in self.COLUMN_MATCHES:
            cls = getattr(desb.md.column_matches, name)
            with self.subTest(column_match=name):
                cls(0, 0, pick_lhs_indices=lambda similarities: [0])
                for invalid in (None, 1, "pick"):
                    with self.assertRaises(TypeError):
                        cls(0, 0, pick_lhs_indices=invalid)

    def test_custom_comparer_requires_a_callable(self):
        custom = desb.md.column_matches.Custom
        custom(lambda left, right: 1.0, 0, 0)
        custom(1, 0, 0, True)  # the classic-measure overload takes any object
        custom(lambda left, right: 1.0, 0, 0, True, column_functions=None)
        custom(
            lambda left, right: 1.0,
            0,
            0,
            column_functions=None,
            pick_lhs_indices=lambda similarities: [0],
        )
        for invalid in (None, 1, "comparer"):
            with self.assertRaises(TypeError):
                custom(invalid, 0, 0)
        custom(lambda left, right: 1.0, 0, 0, pick_lhs_indices=lambda similarities: [0])
        with self.assertRaises(TypeError):
            custom(lambda left, right: 1.0, 0, 0, pick_lhs_indices=None)

    def test_bool_constructor_arguments_are_strict(self):
        custom = desb.md.column_matches.Custom
        comparer = lambda left, right: 1.0
        for value in (True, False):
            custom(comparer, 0, 0, symmetrical=value, equality_is_max=value)
            custom(comparer, 0, 0, value)
        for invalid in (1, 0, None, "true"):
            with self.assertRaises(TypeError):
                custom(comparer, 0, 0, symmetrical=invalid)
            with self.assertRaises(TypeError):
                custom(comparer, 0, 0, equality_is_max=invalid)
            with self.assertRaises(TypeError):
                custom(comparer, 0, 0, invalid)

    def test_docstrings_describe_overloads_and_defaults(self):
        for name in self.COLUMN_MATCHES + ("Custom",):
            cls = getattr(desb.md.column_matches, name)
            with self.subTest(column_match=name):
                doc = cls.__init__.__doc__
                self.assertIsInstance(doc, str)
                self.assertIn("*,", doc)
                self.assertIn("column_functions:", doc)
                self.assertIn("= None", doc)
                self.assertIn("pick_lhs_indices:", doc)
                self.assertIn("= pick_all", doc)
                self.assertNotIn("<nanobind.nb_func", doc)
                if name == "Custom":
                    self.assertIn("min_sim: float = 0.7", doc)
                    self.assertIn("Callable[[object, object], float]", doc)
                    self.assertEqual(doc.count("-> None"), 3)
                else:
                    self.assertIn("minimum_similarity: float = 0.7", doc)
                    self.assertIn("Callable", doc)
                    self.assertEqual(doc.count("-> None"), 2)

                # ``help()`` renders the same overload list as __doc__.
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    help(cls)
                self.assertIn("pick_lhs_indices", output.getvalue())

    def test_inspect_signature_points_to_the_overload_docstring(self):
        for name in self.COLUMN_MATCHES + ("Custom",):
            cls = getattr(desb.md.column_matches, name)
            with self.subTest(column_match=name):
                # nanobind does not expose a per-overload __text_signature__;
                # the class has a generic signature and __init__ carries the
                # detailed overloads.  This is documented in the release notes.
                self.assertEqual(str(inspect.signature(cls)), "(*args, **kwargs)")
                with self.assertRaises(ValueError):
                    inspect.signature(cls.__init__)


class TestPatternProtocols(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.objects = {
            name: _load_pickle(_golden_path(name).read_bytes())
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
                clone = _pickle_roundtrip(value)
                self.assertEqual(value, clone)
                self.assertEqual(hash(value), hash(clone))
                self.assertEqual(len({value, clone}), 1)
                self.assertEqual(len({value, value}), 1)

    def test_copy(self):
        for name, value in self.objects.items():
            for copier in (copy.copy, copy.deepcopy):
                with self.subTest(pattern=name, copier=copier.__name__):
                    clone = _copy_pattern(copier, value)
                    self.assertIsNot(clone, value)
                    self.assertEqual(value, clone)
                    self.assertEqual(hash(value), hash(clone))

    def test_pickle_roundtrip(self):
        for name, value in self.objects.items():
            with self.subTest(pattern=name):
                clone = _pickle_roundtrip(value)
                self.assertIs(type(clone), type(value))
                self.assertEqual(
                    _state_signature(_pickle_state(value)),
                    _state_signature(_pickle_state(clone)),
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

    def test_baseline_state_shape_matches(self):
        for name in sorted(_GOLDEN_TYPES):
            with self.subTest(pattern=name):
                baseline_state = _load_golden_state(name)
                value = _load_pickle(_golden_path(name).read_bytes())
                current_state = _pickle_state(value)
                self.assertEqual(
                    _state_signature(current_state),
                    _state_signature(baseline_state),
                )

    def test_goldens_reencode_to_baseline_bytes(self):
        for name in sorted(_GOLDEN_TYPES):
            # NAR's state contains dictionaries built from std::unordered_map;
            # their iteration order is not part of the pickle format and may
            # differ between standard libraries.  Its contents and recursive
            # types are covered by test_baseline_state_shape_matches.
            if name == "NAR":
                continue
            with self.subTest(pattern=name):
                raw = _golden_path(name).read_bytes()
                value = _load_pickle(raw)
                self.assertEqual(pickle.dumps(value, protocol=_GOLDEN_PICKLE_PROTOCOL), raw)

    def test_goldens_load_and_roundtrip(self):
        names = sorted(_GOLDEN_TYPES)
        for name in names:
            with self.subTest(pattern=name):
                path = _golden_path(name)
                self.assertTrue(path.is_file(), msg=f"missing golden {path}")
                raw = path.read_bytes()
                value = _load_pickle(raw)
                expected_type = _resolve(_GOLDEN_TYPES[name])
                self.assertIsInstance(value, expected_type)

                state = _pickle_state(value)
                self.assertIsInstance(state, tuple)
                self.assertEqual(len(state), _GOLDEN_STATE_ARITY[name])

                clone = _pickle_roundtrip(value)
                self.assertIs(type(clone), type(value))
                self.assertEqual(
                    _state_signature(_pickle_state(clone)),
                    _state_signature(state),
                )
                if name in _CUSTOM_STR_GOLDENS:
                    self.assertEqual(str(clone), str(value))

                if name in _FULL_PROTOCOL_GOLDENS:
                    self.assertEqual(clone, value)
                    self.assertEqual(hash(clone), hash(value))


class TestPickleCopy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.objects = {
            name: _load_pickle(_golden_path(name).read_bytes())
            for name in sorted(_GOLDEN_TYPES)
        }

    def test_copy_and_deepcopy(self):
        for name, value in self.objects.items():
            for copier in (copy.copy, copy.deepcopy):
                with self.subTest(pattern=name, copier=copier.__name__):
                    clone = _copy_pattern(copier, value)
                    self.assertIsNot(clone, value)
                    self.assertIs(type(clone), type(value))
                    self.assertEqual(
                        _state_signature(_pickle_state(clone)),
                        _state_signature(_pickle_state(value)),
                    )
                    if name in _FULL_PROTOCOL_GOLDENS:
                        self.assertEqual(value, clone)
                        self.assertEqual(hash(value), hash(clone))

    def test_reduce_ex_uses_getstate(self):
        for name, value in self.objects.items():
            with self.subTest(pattern=name):
                reduce_value = value.__reduce_ex__(pickle.DEFAULT_PROTOCOL)
                self.assertIsInstance(reduce_value, tuple)
                self.assertEqual(
                    _state_signature(reduce_value[2]),
                    _state_signature(_pickle_state(value)),
                )


class TestPickleStateValidation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.objects = {
            name: _load_pickle(_golden_path(name).read_bytes())
            for name in sorted(_GOLDEN_TYPES)
        }

    def _assert_rejected_without_mutation(self, value, state):
        cls = type(value)
        # This is the same storage state pickle uses: allocated, but no C++
        # object has been constructed in it yet.
        instance = cls.__new__(cls)
        with self.assertRaises(RuntimeError):
            cls.__setstate__(instance, state)
        # A rejected state must not have constructed anything.  The same
        # still-uninitialized storage must accept the valid state afterwards.
        cls.__setstate__(instance, _pickle_state(value))
        self.assertEqual(
            _state_signature(_pickle_state(instance)),
            _state_signature(_pickle_state(value)),
        )

    def test_wrong_arity(self):
        for name, value in self.objects.items():
            state = _pickle_state(value)
            self.assertIsInstance(state, tuple)
            for bad_state in (state[:-1], state + (None,)):
                with self.subTest(pattern=name, arity=len(bad_state)):
                    self._assert_rejected_without_mutation(value, bad_state)

    def test_wrong_field_types(self):
        for name, value in self.objects.items():
            state = _pickle_state(value)
            bad_state = tuple(object() for _ in state)
            with self.subTest(pattern=name):
                self._assert_rejected_without_mutation(value, bad_state)

    def test_nested_fields_are_validated(self):
        cases = {}

        dd_state = _pickle_state(self.objects["DD"])
        dd_left = list(dd_state[0])
        dd_left[0] = dd_left[0][:-1]
        cases["DD"] = (self.objects["DD"], (dd_left, dd_state[1]))

        dc_state = _pickle_state(self.objects["DC"])
        dc_provider = list(dc_state[1])
        dc_provider[0] = dc_provider[0][:-1]
        cases["DC"] = (self.objects["DC"], (dc_state[0], dc_provider, dc_state[2]))

        ind_state = _pickle_state(self.objects["IND"])
        ind_schemas = list(ind_state[2])
        ind_schemas[0] = ind_schemas[0][:-1]
        cases["IND"] = (
            self.objects["IND"],
            (ind_state[0], ind_state[1], ind_schemas, ind_state[3]),
        )

        nar_state = _pickle_state(self.objects["NAR"])
        nar_ante = dict(nar_state[4])
        feature = next(iter(nar_ante))
        _, payload = nar_ante[feature]
        nar_ante[feature] = ("xx", payload)
        cases["NAR"] = (
            self.objects["NAR"],
            (
                nar_state[0],
                nar_state[1],
                nar_state[2],
                nar_state[3],
                nar_ante,
                nar_state[5],
            ),
        )

        ac_state = _pickle_state(self.objects["ACRanges"])
        cases["ACRanges-type"] = (
            self.objects["ACRanges"],
            (ac_state[0], 999, ac_state[2]),
        )
        cases["ACRanges-ranges"] = (
            self.objects["ACRanges"],
            (ac_state[0], ac_state[1], ac_state[2][:-1]),
        )

        for name, (value, bad_state) in cases.items():
            with self.subTest(pattern=name):
                self._assert_rejected_without_mutation(value, bad_state)

    def test_nar_accepts_integer_type_ids(self):
        value = self.objects["NAR"]
        state = _pickle_state(value)

        def integer_type_ids(range_map):
            converted = {}
            for feature, (type_id, payload) in range_map.items():
                if isinstance(type_id, str):
                    self.assertEqual(len(type_id), 1)
                    type_id = ord(type_id)
                converted[feature] = (type_id, payload)
            return converted

        integer_state = (
            state[0],
            state[1],
            state[2],
            state[3],
            integer_type_ids(state[4]),
            integer_type_ids(state[5]),
        )
        clone = desb.nar.NAR.__new__(desb.nar.NAR)
        desb.nar.NAR.__setstate__(clone, integer_state)
        self.assertEqual(
            _state_signature(_pickle_state(clone)),
            _state_signature(state),
        )


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
