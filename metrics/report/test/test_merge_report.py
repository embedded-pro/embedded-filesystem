import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import merge_report  # noqa: E402

NOR = {"sectors": "4096"}


def result(metric="W-1", subject="embedded-filesystem/nor", value=1.0, **fields):
    entry = {
        "metric": metric,
        "workload": "L1",
        "subject": subject,
        "device": "nor-qspi-16m",
        "unit": "B/s",
        "value": value,
        "ramBytes": 7680,
        "better": "higher",
    }
    entry.update(fields)
    return entry


class FragmentDirectory:
    def __init__(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name)
        self.count = 0

    def add(self, results, devices=None):
        self.count += 1
        path = self.path / f"fragment{self.count}.json"
        path.write_text(json.dumps({"devices": devices if devices is not None else {"nor-qspi-16m": NOR}, "results": results}), encoding="utf-8")
        return str(path)

    def cleanup(self):
        self.directory.cleanup()


class MergeTest(unittest.TestCase):
    def setUp(self):
        self.fragments = FragmentDirectory()
        self.addCleanup(self.fragments.cleanup)

    def merged(self, *fragment_results, baseline=None):
        paths = [self.fragments.add(results) for results in fragment_results]
        return merge_report.merge(paths, baseline)["results"]

    def find(self, results, subject):
        return next(entry for entry in results if entry["subject"] == subject)

    def test_merge_combines_the_devices_and_results_of_every_fragment(self):
        first = self.fragments.add([result()], {"nor-qspi-16m": NOR})
        second = self.fragments.add([result(subject="littlefs/default")], {"sd-card-256m": {"blocks": "524288"}})

        report = merge_report.merge([first, second])

        self.assertEqual({"nor-qspi-16m": NOR, "sd-card-256m": {"blocks": "524288"}}, report["devices"])
        self.assertEqual(2, len(report["results"]))

    def test_merge_rejects_a_device_described_differently_by_two_fragments(self):
        first = self.fragments.add([], {"nor-qspi-16m": NOR})
        second = self.fragments.add([], {"nor-qspi-16m": {"sectors": "256"}})

        with self.assertRaises(merge_report.ReportError):
            merge_report.merge([first, second])

    def test_at_least_the_best_baseline_compares_with_the_highest_baseline(self):
        results = self.merged(
            [result(value=10.0, target=">= baselines")],
            [result(subject="littlefs/default", value=12.0), result(subject="fatfs/tuned", value=8.0)],
        )

        own = self.find(results, "embedded-filesystem/nor")
        self.assertFalse(own["pass"])
        self.assertEqual("littlefs/default", own["referenceSubject"])
        self.assertEqual(12.0, own["referenceValue"])

    def test_at_least_the_best_baseline_passes_when_it_is_reached(self):
        results = self.merged([result(value=12.0, target=">= baselines")], [result(subject="littlefs/default", value=12.0)])

        self.assertTrue(self.find(results, "embedded-filesystem/nor")["pass"])

    def test_a_factor_of_the_best_baseline_passes_when_the_value_reaches_the_factor_times_the_baseline(self):
        results = self.merged([result(value=97.0, target=">= 0.97 baselines")], [result(subject="littlefs/default", value=100.0)])

        own = self.find(results, "embedded-filesystem/nor")
        self.assertTrue(own["pass"])
        self.assertEqual(100.0, own["referenceValue"])

    def test_a_factor_of_the_best_baseline_fails_below_the_factor_times_the_baseline(self):
        results = self.merged([result(value=96.0, target=">= 0.97 baselines")], [result(subject="littlefs/default", value=100.0)])

        self.assertFalse(self.find(results, "embedded-filesystem/nor")["pass"])

    def test_a_factor_in_exponent_notation_scales_the_best_baseline(self):
        results = self.merged([result(value=1500.0, target=">= 1e+01 baselines")], [result(subject="littlefs/default", value=100.0)])

        self.assertTrue(self.find(results, "embedded-filesystem/nor")["pass"])

    def test_at_most_a_family_compares_with_its_lowest_member(self):
        results = self.merged(
            [result(metric="M-3", value=4.0, target="<= fatfs", better="lower")],
            [result(metric="M-3", subject="fatfs/default", value=5.0), result(metric="M-3", subject="fatfs/tuned", value=3.0)],
        )

        own = self.find(results, "embedded-filesystem/nor")
        self.assertFalse(own["pass"])
        self.assertEqual("fatfs/tuned", own["referenceSubject"])

    def test_a_relative_target_only_compares_the_same_metric_workload_device_and_fill(self):
        results = self.merged(
            [result(value=10.0, target=">= baselines", fill=10)],
            [
                result(subject="littlefs/default", value=99.0, fill=50),
                result(subject="littlefs/default", value=99.0, fill=10, device="internal-flash"),
                result(subject="littlefs/default", value=99.0, fill=10, workload="L2"),
                result(metric="W-4", subject="littlefs/default", value=99.0, fill=10),
                result(subject="littlefs/default", value=9.0, fill=10),
            ],
        )

        own = self.find(results, "embedded-filesystem/nor")
        self.assertTrue(own["pass"])
        self.assertEqual(9.0, own["referenceValue"])

    def test_the_own_subjects_are_not_baselines(self):
        results = self.merged([result(value=10.0, target=">= baselines"), result(subject="embedded-filesystem/fat", value=99.0)])

        own = self.find(results, "embedded-filesystem/nor")
        self.assertNotIn("pass", own)
        self.assertIn("note", own)

    def test_a_result_without_a_value_misses_a_relative_target(self):
        results = self.merged([result(value=None, target=">= baselines")], [result(subject="littlefs/default", value=1.0)])

        self.assertFalse(self.find(results, "embedded-filesystem/nor")["pass"])

    def test_an_absolute_target_keeps_the_decision_of_the_metric_executable(self):
        results = self.merged([result(target="<= 1.1", value=1.2, **{"pass": False})])

        self.assertFalse(results[0]["pass"])
        self.assertNotIn("referenceValue", results[0])

    def test_an_unknown_target_is_rejected(self):
        with self.assertRaises(merge_report.ReportError):
            self.merged([result(target="about 3")])

    def test_results_are_sorted_by_metric_number(self):
        results = self.merged([result(metric="W-10"), result(metric="W-2"), result(metric="R-1")])

        self.assertEqual(["R-1", "W-2", "W-10"], [entry["metric"] for entry in results])


class RegressionTest(unittest.TestCase):
    def setUp(self):
        self.fragments = FragmentDirectory()
        self.addCleanup(self.fragments.cleanup)

    def compared(self, current, earlier, earlier_devices=None):
        baseline = {"devices": earlier_devices if earlier_devices is not None else {"nor-qspi-16m": NOR}, "results": [earlier]}
        return merge_report.merge([self.fragments.add([current])], baseline)["results"][0]

    def test_a_reported_metric_worse_by_more_than_5_percent_fails(self):
        compared = self.compared(result(value=94.0), result(value=100.0))

        self.assertTrue(compared["regression"])
        self.assertFalse(compared["pass"])
        self.assertEqual(100.0, compared["baselineValue"])

    def test_a_diagnostic_worse_by_more_than_5_percent_is_not_a_regression(self):
        compared = self.compared(result(value=80.0, diagnostic=True), result(value=100.0))

        self.assertNotIn("regression", compared)
        self.assertNotIn("pass", compared)
        self.assertEqual(100.0, compared["baselineValue"])

    def test_a_reported_metric_worse_by_less_than_5_percent_passes(self):
        compared = self.compared(result(value=96.0), result(value=100.0))

        self.assertNotIn("regression", compared)
        self.assertNotIn("pass", compared)

    def test_lower_is_better_regresses_when_the_value_grows(self):
        compared = self.compared(result(value=1.06, better="lower"), result(value=1.0, better="lower"))

        self.assertTrue(compared["regression"])

    def test_lower_is_better_improves_when_the_value_shrinks(self):
        compared = self.compared(result(value=0.5, better="lower"), result(value=1.0, better="lower"))

        self.assertNotIn("regression", compared)

    def test_baseline_subjects_never_regress(self):
        compared = self.compared(result(subject="fatfs/default", value=50.0), result(subject="fatfs/default", value=100.0))

        self.assertNotIn("regression", compared)
        self.assertEqual(100.0, compared["baselineValue"])

    def test_metrics_with_a_target_never_regress(self):
        compared = self.compared(result(value=50.0, target="<= 100"), result(value=10.0, target="<= 100"))

        self.assertNotIn("regression", compared)

    def test_a_result_that_was_diagnostic_on_main_is_not_compared(self):
        compared = self.compared(result(value=50.0), result(value=100.0, diagnostic=True))

        self.assertNotIn("regression", compared)
        self.assertNotIn("pass", compared)

    def test_a_changed_device_model_is_not_compared(self):
        compared = self.compared(result(value=50.0), result(value=100.0), {"nor-qspi-16m": {"sectors": "256"}})

        self.assertNotIn("regression", compared)
        self.assertEqual("device model changed since the baseline", compared["note"])

    def test_a_result_missing_from_the_baseline_is_not_compared(self):
        compared = self.compared(result(value=50.0), result(metric="R-1", value=100.0))

        self.assertNotIn("baselineValue", compared)


class MarkdownTest(unittest.TestCase):
    def report(self, *results, devices=None):
        return {"devices": devices if devices is not None else {"nor-qspi-16m": NOR}, "results": list(results)}

    def test_markdown_summarizes_the_outcomes(self):
        text = merge_report.markdown(self.report(result(target="<= 1", **{"pass": True}), result(metric="W-2"), result(metric="W-4", **{"pass": False})))

        self.assertIn("3 results: 1 passed, 1 failed, 1 reported.", text)

    def test_markdown_gives_the_lifetime_of_each_own_subject_against_the_best_littlefs(self):
        text = merge_report.markdown(
            self.report(
                result(metric="WL-3", workload="L5", value=900.0),
                result(metric="WL-3", workload="L5", subject="littlefs/default", value=300.0),
                result(metric="WL-3", workload="L5", subject="littlefs/ram-matched", value=200.0),
            )
        )

        self.assertIn("WL-3 on nor-qspi-16m: embedded-filesystem/nor writes 3 times the user data of littlefs/default.", text)

    def test_markdown_without_a_littlefs_lifetime_gives_no_ratio(self):
        text = merge_report.markdown(self.report(result(metric="WL-3", workload="L5", value=900.0)))

        self.assertNotIn("times the user data", text)

    def test_markdown_has_a_table_per_device_with_a_column_per_subject(self):
        text = merge_report.markdown(
            self.report(
                result(subject="littlefs/default", value=2.0),
                result(subject="embedded-filesystem/nor", value=1.02345, target=">= baselines", **{"pass": True}),
                result(subject="fatfs/default", device="sd-card-256m", value=3.0),
            )
        )

        self.assertIn("### nor-qspi-16m", text)
        self.assertIn("### sd-card-256m", text)
        self.assertIn("| Metric | Workload | Fill | Unit | embedded-filesystem/nor | littlefs/default |", text)
        self.assertIn("| W-1 | L1 |  | B/s | 1.023 ✅ | 2 |", text)
        self.assertIn("| RAM |  |  | B | 7680 | 7680 |", text)

    def test_markdown_rows_follow_the_metric_number_the_workload_numbers_and_the_fill(self):
        text = merge_report.markdown(
            self.report(
                result(metric="W-10", workload="L1"),
                result(metric="W-2", workload="L1 4096 64"),
                result(metric="W-2", workload="L1 4096 512"),
                result(metric="W-2", workload="L1 512 64", fill=50),
                result(metric="W-2", workload="L1 512 64", fill=10),
            )
        )

        rows = [line.split(" | ")[0:3] for line in text.splitlines() if line.startswith("| W-")]
        self.assertEqual(
            [["| W-2", "L1 512 64", "10 %"], ["| W-2", "L1 512 64", "50 %"], ["| W-2", "L1 4096 64", ""], ["| W-2", "L1 4096 512", ""], ["| W-10", "L1", ""]],
            rows,
        )

    def test_markdown_leaves_a_cell_empty_when_a_subject_has_no_result(self):
        text = merge_report.markdown(self.report(result(subject="littlefs/default", metric="W-1"), result(subject="littlefs/tuned", metric="W-2")))

        self.assertIn("| W-1 | L1 |  | B/s | 1 |  |", text)

    def test_markdown_shows_values_that_cannot_be_measured(self):
        text = merge_report.markdown(self.report(result(value=None)))

        self.assertIn("| W-1 | L1 |  | B/s | n/a |", text)

    def test_markdown_lists_missed_targets_and_regressions(self):
        text = merge_report.markdown(
            self.report(
                result(metric="W-2", value=1.5, unit="ratio", fill=50, target="<= 1.1", **{"pass": False}),
                result(value=94.0, baselineValue=100.0, regression=True, **{"pass": False}),
            )
        )

        self.assertIn("### Missed targets and regressions", text)
        self.assertIn("- W-2 L1 at 50 %, embedded-filesystem/nor on nor-qspi-16m: 1.5 ratio, expected <= 1.1", text)
        self.assertIn("- W-1 L1, embedded-filesystem/nor on nor-qspi-16m: 94 B/s, expected main: 100", text)
        self.assertIn("94 ❌ (main: 100)", text)

    def test_markdown_without_failures_has_no_failure_list(self):
        self.assertNotIn("Missed targets", merge_report.markdown(self.report(result())))

    def test_markdown_describes_the_device_models(self):
        self.assertIn("| nor-qspi-16m | sectors | 4096 |", merge_report.markdown(self.report(result())))


class MainTest(unittest.TestCase):
    def setUp(self):
        self.fragments = FragmentDirectory()
        self.addCleanup(self.fragments.cleanup)
        self.json = self.fragments.path / "out" / "metrics.json"
        self.markdown = self.fragments.path / "out" / "metrics.md"
        self.json.parent.mkdir()

    def run_main(self, *extra):
        arguments = ["--fragment-directory", str(self.fragments.path), "--json", str(self.json), "--markdown", str(self.markdown)]
        return merge_report.main([*arguments, *extra])

    def test_main_writes_the_merged_report_and_the_summary(self):
        self.fragments.add([result()])

        self.assertEqual(0, self.run_main())

        self.assertEqual(1, len(json.loads(self.json.read_text(encoding="utf-8"))["results"]))
        self.assertIn("## Metrics", self.markdown.read_text(encoding="utf-8"))

    def test_main_with_check_fails_when_a_target_is_missed(self):
        self.fragments.add([result(target="<= 1", value=2.0, **{"pass": False})])

        self.assertEqual(1, self.run_main("--check"))

    def test_main_without_check_succeeds_when_a_target_is_missed(self):
        self.fragments.add([result(target="<= 1", value=2.0, **{"pass": False})])

        self.assertEqual(0, self.run_main())

    def test_main_reads_the_baseline(self):
        self.fragments.add([result(value=50.0)])
        baseline = self.fragments.path / "out" / "main.json"
        baseline.write_text(json.dumps({"devices": {"nor-qspi-16m": NOR}, "results": [result(value=100.0)]}), encoding="utf-8")

        self.assertEqual(1, self.run_main("--check", "--baseline", str(baseline)))

    def test_main_ignores_a_missing_baseline(self):
        self.fragments.add([result()])

        self.assertEqual(0, self.run_main("--check", "--baseline", str(self.fragments.path / "missing.json")))

    def test_main_takes_the_fragments_of_every_fragment_directory(self):
        self.fragments.add([result()])
        other = FragmentDirectory()
        self.addCleanup(other.cleanup)
        other.add([result(subject="littlefs/default")], {"stm32h757i-eval/qspi": {"run": "2026_10_20"}})

        self.assertEqual(0, self.run_main("--fragment-directory", str(other.path)))

        report = json.loads(self.json.read_text(encoding="utf-8"))
        self.assertEqual(2, len(report["results"]))
        self.assertIn("stm32h757i-eval/qspi", report["devices"])

    def test_main_warns_about_a_missing_fragment_directory(self):
        self.fragments.add([result()])
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            self.assertEqual(0, self.run_main("--fragment-directory", str(self.fragments.path / "missing")))

        self.assertIn("no fragment directory", stderr.getvalue())

    def test_main_reports_conflicting_fragments(self):
        self.fragments.add([], {"nor-qspi-16m": NOR})
        self.fragments.add([], {"nor-qspi-16m": {"sectors": "1"}})

        self.assertEqual(2, self.run_main())


if __name__ == "__main__":
    unittest.main()
