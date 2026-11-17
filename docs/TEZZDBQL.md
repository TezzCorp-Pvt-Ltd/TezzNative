# TezzDBQL contract

TezzDBQL supports a bounded SQL subset over TezzDB JSON rows:

```sql
SELECT * FROM table [WHERE column = value] [LIMIT value]
INSERT INTO table (column, ...) VALUES (value, ...)
UPDATE table SET column = value, ... [WHERE column = value]
DELETE FROM table [WHERE column = value]
CREATE TABLE table
DROP TABLE table
```

Values may be `?` parameters, quoted strings, or unquoted literal tokens. A quote
inside a quoted SQL string is doubled, as in `'it''s'`. Parameters are string
values; they never become SQL syntax. The supplied parameter count must match
the number of placeholders. Column lists and assignment lists allow at most 32
distinct columns. An optional final semicolon is accepted; trailing statements
or unsupported clauses are errors.

The complete statement is validated before execution. Malformed statements,
missing parameters, and excess columns return an error without changing rows.
Identifiers are case insensitive. INSERT values use the existing string-valued
JSON row convention; `cursor_field_int` also reads decimal string values.

`id`, `pk`, and `_pk` in WHERE clauses refer to the actual TezzDB primary key,
including rows whose JSON does not contain an ID. Other predicates compare the
stored field value. UPDATE preserves fields outside the SET list, including
nested JSON values, and adds newly assigned fields. UPDATE or DELETE without
WHERE affects all rows. Mutations collect primary keys before changing rows so
deleting or resizing rows does not skip records.

Mutations use a transaction when the caller has not already opened one. A caller
that supplies an existing transaction retains responsibility for committing or
rolling it back after a storage error. Cursor/storage failures are reported as
query errors instead of returning partial successful results.

Query results and extracted strings are owned by the caller. Free results with
`db_result_free` and strings with `free`. The deterministic fixtures in
`tests/regression/database/ql_*.tn` cover query semantics, syntax boundaries,
parameter handling, JSON escaping, integer limits, and cursor errors.
