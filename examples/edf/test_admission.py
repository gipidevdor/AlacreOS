import unittest

from admission import Task, overload


class AdmissionTests(unittest.TestCase):
    def test_reference_workloads(self):
        self.assertIsNone(overload((Task(40, 100, 1000), Task(50, 100, 1000))))
        self.assertEqual(overload((Task(60, 100, 1000), Task(60, 100, 1000))), (100, 120))

    def test_overload_can_appear_after_both_initial_deadlines(self):
        tasks = (Task(2, 2, 3), Task(2, 4, 6))
        self.assertEqual(overload(tasks), (5, 6))

    def test_demand_equal_to_capacity_is_feasible(self):
        self.assertIsNone(overload((Task(2, 2, 4), Task(2, 4, 4))))


if __name__ == "__main__":
    unittest.main()
