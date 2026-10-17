"""用 JSON Schema 校验示例文件。没有 jsonschema 库时返回 77（CTest 记为跳过）。
注意：Schema 只用于文档与编辑器提示，规则以 C++ WorkflowValidator 为准。"""
import json
import sys

try:
    import jsonschema
except ImportError:
    print("jsonschema not installed, skipping")
    sys.exit(77)

schema_path, *files = sys.argv[1:]
with open(schema_path, encoding="utf-8") as f:
    schema = json.load(f)
jsonschema.Draft7Validator.check_schema(schema)
validator = jsonschema.Draft7Validator(schema)

failed = False
for path in files:
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    errors = sorted(validator.iter_errors(doc), key=lambda e: list(e.path))
    if errors:
        failed = True
        for e in errors:
            print(f"{path}: {'/'.join(map(str, e.path))}: {e.message}")
    else:
        print(f"{path}: OK")
sys.exit(1 if failed else 0)
