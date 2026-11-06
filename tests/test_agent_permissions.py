import ast
import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit(message)


source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
tree = ast.parse(source)
assignments = [
    node
    for node in ast.walk(tree)
    if isinstance(node, ast.Assign)
    and any(isinstance(target, ast.Name) and target.id == "AUTO_APPROVE" for target in node.targets)
]
if len(assignments) != 1:
    fail("AUTO_APPROVE must have exactly one configuration assignment")

initializer = assignments[0].value
if not isinstance(initializer, ast.Compare) or not initializer.comparators:
    fail("AUTO_APPROVE must be controlled by an explicit environment check")
call = initializer.left
if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Attribute) or call.func.attr != "lower":
    fail("AUTO_APPROVE should normalize its environment value")
getenv = call.func.value
if not isinstance(getenv, ast.Call) or not isinstance(getenv.func, ast.Attribute) or getenv.func.attr != "getenv":
    fail("AUTO_APPROVE must be read from the environment")
if len(getenv.args) < 2 or not isinstance(getenv.args[1], ast.Constant) or getenv.args[1].value != "false":
    fail("automatic approval must default to disabled")
if initializer.comparators[0].value != "true":
    fail("automatic approval must require an explicit true opt-in")

parents = {}
for parent in ast.walk(tree):
    for child in ast.iter_child_nodes(parent):
        parents[child] = parent

approval_calls = [
    node
    for node in ast.walk(tree)
    if isinstance(node, ast.Attribute) and node.attr == "approve_all"
]
for approval in approval_calls:
    ancestor = parents.get(approval)
    guarded = False
    while ancestor is not None:
        if isinstance(ancestor, ast.If) and isinstance(ancestor.test, ast.Name) and ancestor.test.id == "AUTO_APPROVE":
            guarded = True
            break
        ancestor = parents.get(ancestor)
    if not guarded:
        fail("PermissionHandler.approve_all must only be selected under AUTO_APPROVE")

print("Agent permission defaults are opt-in only")
