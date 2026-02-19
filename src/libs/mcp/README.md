# How to generate the schema files

```bash
# Run from the root of the repository
python3 ./src/libs/mcp/schemas/generate_cpp_from_schema.py \
    ./src/libs/mcp/schemas/schema-2025-11-25.json \
    ./src/libs/mcp/schemas/schema_2025_11_25.h \
    --namespace Mcp::Generated::Schema::_2025_11_25
```
