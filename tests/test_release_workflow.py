from __future__ import annotations

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "build.yml"


def _step_block(workflow: str, name: str) -> str:
    lines = workflow.splitlines()
    marker = f"      - name: {name}"
    try:
        start = lines.index(marker)
    except ValueError:
        return ""
    end = next(
        (index for index in range(start + 1, len(lines))
         if lines[index].startswith("      - name: ")),
        len(lines),
    )
    return chr(10).join(lines[start:end])


class ReleaseWorkflowPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW_PATH.read_text(encoding="utf-8")

    def test_installable_product_artifact_is_private_repository_only(self) -> None:
        block = _step_block(
            self.workflow,
            "Upload installable VST3 bundle and provenance manifest",
        )
        self.assertTrue(block, "Installable VST3 artifact upload step is missing.")
        self.assertIn(
            "if: ${{ github.event.repository.private == true }}",
            block,
        )
        self.assertIn("name: StopDial-Windows-x64-VST3", block)
        self.assertIn("path: package/", block)

    def test_public_repository_run_explains_withheld_product_artifact(self) -> None:
        block = _step_block(
            self.workflow,
            "Explain public-repository artifact withholding",
        )
        self.assertTrue(block, "Public-repository notice step is missing.")
        self.assertIn(
            "if: ${{ github.event.repository.private != true }}",
            block,
        )
        self.assertIn("::notice::", block)
        self.assertIn("withheld because this repository is public", block)

    def test_verifier_job_discovers_all_release_contract_tests(self) -> None:
        self.assertIn(
            'python -m unittest discover -s tests -p "test_*.py" -v',
            self.workflow,
        )


if __name__ == "__main__":
    unittest.main()
