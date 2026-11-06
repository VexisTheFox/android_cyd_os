#!/usr/bin/env python3
"""
CYD Android-like OS Prototype — Automated E2E Test Suite Runner
Authoritative Specifications: PROJECT.md & ORIGINAL_REQUEST.md
"""
import sys
import os
import time
import json
import argparse
import unittest
from typing import Dict, Any, List

# Ensure project root is in sys.path
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)


def discover_suite(tier: int = None, feature: str = None) -> unittest.TestSuite:
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    tier_dirs = {
        1: os.path.join(PROJECT_ROOT, "test", "test_tier1_features"),
        2: os.path.join(PROJECT_ROOT, "test", "test_tier2_boundaries"),
        3: os.path.join(PROJECT_ROOT, "test", "test_tier3_interactions"),
        4: os.path.join(PROJECT_ROOT, "test", "test_tier4_scenarios"),
    }

    target_tiers = [tier] if tier in tier_dirs else [1, 2, 3, 4]

    for t in target_tiers:
        tier_suite = loader.discover(start_dir=tier_dirs[t], pattern="test_*.py")
        if feature:
            # Filter by feature ID (e.g. F01, B01, etc.)
            filtered_suite = unittest.TestSuite()
            f_norm = feature.upper()
            b_norm = f"B{f_norm[1:]}" if f_norm.startswith("F") else f_norm
            for test_group in tier_suite:
                for test_case in test_group:
                    case_str = str(test_case).upper()
                    class_name = test_case.__class__.__name__.upper()
                    if f_norm in case_str or f_norm in class_name or b_norm in case_str or b_norm in class_name:
                        filtered_suite.addTest(test_case)
            suite.addTests(filtered_suite)
        else:
            suite.addTests(tier_suite)

    return suite


class StructuredTestResult(unittest.TestResult):
    def __init__(self, stream=None, descriptions=None, verbosity=1):
        super().__init__(stream, descriptions, verbosity)
        self.verbosity = verbosity
        self.stream = stream or sys.stdout
        self.test_records: List[Dict[str, Any]] = []
        self._start_time = 0.0

    def startTest(self, test):
        super().startTest(test)
        self._test_start = time.time()
        if self.verbosity > 1:
            self.stream.write(f"  RUN: {test.id()} ... ")
            self.stream.flush()

    def addSuccess(self, test):
        super().addSuccess(test)
        elapsed = time.time() - self._test_start
        self.test_records.append({
            "test_id": test.id(),
            "status": "PASS",
            "duration": elapsed,
            "error": None
        })
        if self.verbosity > 1:
            self.stream.write(f"PASSED ({elapsed:.3f}s)\n")
        elif self.verbosity == 1:
            self.stream.write(".")
            self.stream.flush()

    def addFailure(self, test, err):
        super().addFailure(test, err)
        elapsed = time.time() - self._test_start
        self.test_records.append({
            "test_id": test.id(),
            "status": "FAIL",
            "duration": elapsed,
            "error": self._exc_info_to_string(err, test)
        })
        if self.verbosity > 1:
            self.stream.write(f"FAILED ({elapsed:.3f}s)\n")
        elif self.verbosity == 1:
            self.stream.write("F")
            self.stream.flush()

    def addError(self, test, err):
        super().addError(test, err)
        elapsed = time.time() - self._test_start
        self.test_records.append({
            "test_id": test.id(),
            "status": "ERROR",
            "duration": elapsed,
            "error": self._exc_info_to_string(err, test)
        })
        if self.verbosity > 1:
            self.stream.write(f"ERROR ({elapsed:.3f}s)\n")
        elif self.verbosity == 1:
            self.stream.write("E")
            self.stream.flush()


def run_e2e_tests():
    parser = argparse.ArgumentParser(description="CYD Android OS E2E Automated Test Runner")
    parser.add_argument("--tier", type=int, choices=[1, 2, 3, 4], help="Run a specific test tier (1-4)")
    parser.add_argument("--feature", type=str, help="Filter by specific feature (e.g., F01..F24)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose test execution output")
    parser.add_argument("--json-report", type=str, help="Output path for JSON structured report")
    args = parser.parse_args()

    print("================================================================================")
    print("      CYD Android-like OS Prototype — Automated E2E Test Suite Runner           ")
    print("      Target Hardware: ESP32-2432S028 (320x240 LCD + Resistive Touch)           ")
    print("================================================================================")
    if args.tier:
        print(f"[*] Scope: Tier {args.tier} only")
    if args.feature:
        print(f"[*] Scope: Feature {args.feature.upper()} only")
    if not args.tier and not args.feature:
        print("[*] Scope: Full 4-Tier Test Suite (Tiers 1-4, 24 Features, 260+ Tests)")
    print("--------------------------------------------------------------------------------")

    suite = discover_suite(tier=args.tier, feature=args.feature)
    total_tests = suite.countTestCases()
    if total_tests == 0:
        print("[!] No tests matched the specified criteria.")
        sys.exit(2)

    verbosity = 2 if args.verbose else 1
    result = StructuredTestResult(verbosity=verbosity)

    start_wall = time.time()
    suite.run(result)
    total_duration = time.time() - start_wall

    if verbosity == 1:
        print()  # newline after dots

    # Categorize results by Tier
    tier_counts = {1: {"pass": 0, "fail": 0}, 2: {"pass": 0, "fail": 0}, 3: {"pass": 0, "fail": 0}, 4: {"pass": 0, "fail": 0}}
    for rec in result.test_records:
        tid = rec["test_id"]
        status = rec["status"]
        t_num = 1
        if "test_b" in tid or "Bounds" in tid:
            t_num = 2
        elif "test_interactions" in tid or "Tier3" in tid or "test_ci" in tid:
            t_num = 3
        elif "test_scenarios" in tid or "Tier4" in tid or "test_scenario" in tid:
            t_num = 4

        if status == "PASS":
            tier_counts[t_num]["pass"] += 1
        else:
            tier_counts[t_num]["fail"] += 1

    passed_count = len(result.test_records) - len(result.failures) - len(result.errors)
    failed_count = len(result.failures) + len(result.errors)

    print("\n================================================================================")
    print("                               E2E TEST SUMMARY                                 ")
    print("================================================================================")
    print(f" Tier 1 (Feature Coverage):            {tier_counts[1]['pass']} Passed | {tier_counts[1]['fail']} Failed")
    print(f" Tier 2 (Boundary & Corner Cases):     {tier_counts[2]['pass']} Passed | {tier_counts[2]['fail']} Failed")
    print(f" Tier 3 (Cross-Feature Interactions):  {tier_counts[3]['pass']} Passed | {tier_counts[3]['fail']} Failed")
    print(f" Tier 4 (Real-World Scenarios):        {tier_counts[4]['pass']} Passed | {tier_counts[4]['fail']} Failed")
    print("--------------------------------------------------------------------------------")
    print(f" TOTAL TESTS RUN:  {len(result.test_records)}")
    print(f" PASSED:           {passed_count} ({passed_count / max(1, len(result.test_records)) * 100:.1f}%)")
    print(f" FAILED:           {failed_count}")
    print(f" TOTAL TIME:       {total_duration:.3f} seconds")
    print("================================================================================")

    # Print failures if any
    if result.failures:
        print("\n--- FAILURES ---")
        for test, err in result.failures:
            print(f"\n[FAIL] {test.id()}:\n{err}")
    if result.errors:
        print("\n--- ERRORS ---")
        for test, err in result.errors:
            print(f"\n[ERROR] {test.id()}:\n{err}")

    # Generate JSON report if requested
    if args.json_report:
        report_data = {
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "total": len(result.test_records),
            "passed": passed_count,
            "failed": failed_count,
            "duration": total_duration,
            "tiers": tier_counts,
            "tests": result.test_records
        }
        with open(args.json_report, "w") as f:
            json.dump(report_data, f, indent=2)
        print(f"[*] Structured JSON report saved to: {args.json_report}")

    if failed_count == 0:
        print("\n[SUCCESS] ALL E2E TESTS PASSED CLEANLY (100% SUCCESS RATE)\n")
        sys.exit(0)
    else:
        print(f"\n[FAILURE] {failed_count} TEST(S) FAILED\n")
        sys.exit(1)


if __name__ == "__main__":
    run_e2e_tests()
