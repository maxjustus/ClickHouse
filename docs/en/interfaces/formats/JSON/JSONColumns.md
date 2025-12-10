---
alias: []
description: 'Documentation for the JSONColumns format'
input_format: true
keywords: ['JSONColumns']
output_format: true
slug: /interfaces/formats/JSONColumns
title: 'JSONColumns'
doc_type: 'reference'
---

| Input | Output | Alias |
|-------|--------|-------|
| ✔     | ✔      |       |

## Description {#description}

:::tip
The output of the JSONColumns* formats provides the ClickHouse field name and then the content of each row in the table for that field;
visually, the data is rotated 90 degrees to the left.
:::

In this format, data is represented as JSON objects (one or more). By default, all data is output as a single JSON Object. The input side supports multiple newline-separated JSON blocks for streaming data without buffering.

:::note
The `JSONColumns` format buffers all data in memory. When outputting, by default all data is output as a single block, which can lead to high memory consumption. Use the `output_format_json_columns_block_size` setting to split output into multiple blocks for streaming scenarios.
:::

## Example usage {#example-usage}

### Inserting data {#inserting-data}

Using a JSON file with the following data, named as `football.json`:

```json
{
    "date": ["2022-04-30", "2022-04-30", "2022-04-30", "2022-05-02", "2022-05-02", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07"],
    "season": [2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021],
    "home_team": ["Sutton United", "Swindon Town", "Tranmere Rovers", "Port Vale", "Salford City", "Barrow", "Bradford City", "Bristol Rovers", "Exeter City", "Harrogate Town A.F.C.", "Hartlepool United", "Leyton Orient", "Mansfield Town", "Newport County", "Oldham Athletic", "Stevenage Borough", "Walsall"],
    "away_team": ["Bradford City", "Barrow", "Oldham Athletic", "Newport County", "Mansfield Town", "Northampton Town", "Carlisle United", "Scunthorpe United", "Port Vale", "Sutton United", "Colchester United", "Tranmere Rovers", "Forest Green Rovers", "Rochdale", "Crawley Town", "Salford City", "Swindon Town"],
    "home_team_goals": [1, 2, 2, 1, 2, 1, 2, 7, 0, 0, 0, 0, 2, 0, 3, 4, 0],
    "away_team_goals": [4, 1, 0, 2, 2, 3, 0, 0, 1, 2, 2, 1, 2, 2, 3, 2, 3]
}
```

Insert the data:

```sql
INSERT INTO football FROM INFILE 'football.json' FORMAT JSONColumns;
```

### Reading data {#reading-data}

Read data using the `JSONColumns` format:

```sql
SELECT *
FROM football
FORMAT JSONColumns
```

The output will be in JSON format:

```json
{
    "date": ["2022-04-30", "2022-04-30", "2022-04-30", "2022-05-02", "2022-05-02", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07", "2022-05-07"],
    "season": [2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021, 2021],
    "home_team": ["Sutton United", "Swindon Town", "Tranmere Rovers", "Port Vale", "Salford City", "Barrow", "Bradford City", "Bristol Rovers", "Exeter City", "Harrogate Town A.F.C.", "Hartlepool United", "Leyton Orient", "Mansfield Town", "Newport County", "Oldham Athletic", "Stevenage Borough", "Walsall"],
    "away_team": ["Bradford City", "Barrow", "Oldham Athletic", "Newport County", "Mansfield Town", "Northampton Town", "Carlisle United", "Scunthorpe United", "Port Vale", "Sutton United", "Colchester United", "Tranmere Rovers", "Forest Green Rovers", "Rochdale", "Crawley Town", "Salford City", "Swindon Town"],
    "home_team_goals": [1, 2, 2, 1, 2, 1, 2, 7, 0, 0, 0, 0, 2, 0, 3, 4, 0],
    "away_team_goals": [4, 1, 0, 2, 2, 3, 0, 0, 1, 2, 2, 1, 2, 2, 3, 2, 3]
}
```

## Multiple blocks support {#multiple-blocks}

The `JSONColumns` format supports reading and writing multiple JSON blocks for streaming scenarios.

### Input: Multiple blocks {#input-multiple-blocks}

When inserting, you can provide multiple JSON blocks separated by newlines. Each block represents a set of rows and will be processed as it arrives, enabling progressive processing without buffering the entire dataset in memory.

Example:

```json
{"a": [1, 2, 3], "b": ["x", "y", "z"]}
{"a": [4, 5, 6], "b": ["p", "q", "r"]}
```

All blocks must have the same set of columns (though column order can vary).

### Output: Split into blocks {#output-multiple-blocks}

Use the `output_format_json_columns_block_size` setting to split output into multiple blocks. When set to a positive value N, rows are split into blocks of N rows each, with each block output as a separate JSON object.

Example with `output_format_json_columns_block_size=2`:

```json
{"a": [1, 2], "b": ["x", "y"]}
{"a": [3, 4], "b": ["z", "p"]}
```

This is useful for streaming output to clients that process blocks independently.

## Format settings {#format-settings}

- `input_format_skip_unknown_fields` ([`input_format_skip_unknown_fields`](/operations/settings/settings-formats.md/#input_format_skip_unknown_fields)): Skip columns with unknown names. Enabled by default.

- `input_format_defaults_for_omitted_fields` ([`input_format_defaults_for_omitted_fields`](/operations/settings/settings-formats.md/#input_format_defaults_for_omitted_fields)): Fill omitted columns with default values.

- `output_format_json_columns_block_size`: When set to a positive integer N, splits output rows into blocks of N rows each. Default: 0 (output as single block).

Example:

```sql
SELECT * FROM table FORMAT JSONColumns SETTINGS output_format_json_columns_block_size=100;
```