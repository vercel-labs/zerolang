## When To Use std.csv

In Zerolang, use `std.csv` for allocation-free CSV validation, record scanning,
field decoding, and small fixed-arity CSV writers.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.csv.valid(bytes)` | `Bool` | Validates bounded CSV input with quoted fields and CRLF or LF records. |
| `std.csv.recordCount(bytes)` | `Maybe<usize>` | Counts valid records, returning null on malformed input. |
| `std.csv.record(bytes, index)` | `Maybe<Span<u8>>` | Borrows one record slice by ordinal, excluding the line terminator. |
| `std.csv.fieldCount(record)` | `Maybe<usize>` | Counts fields in one valid record. |
| `std.csv.field(buffer, record, index)` | `Maybe<Span<u8>>` | Decodes one field into caller storage. |
| `std.csv.encodedFieldLen(field)` | `usize` | Computes the bytes needed to write one CSV field. |
| `std.csv.writeField(buffer, field)` | `Maybe<Span<u8>>` | Writes one CSV field with quotes when required. |
| `std.csv.writeRecord2(buffer, left, right)` | `Maybe<Span<u8>>` | Writes a two-field record ending in `\n`. |
| `std.csv.writeRecord3(buffer, first, second, third)` | `Maybe<Span<u8>>` | Writes a three-field record ending in `\n`. |

Metadata labels:

- effects: parse
- allocation behavior: no allocation; decoded fields and writer output use caller storage
- target support: target-neutral
- error behavior: `Maybe` helpers return null on malformed input or insufficient storage
- ownership notes: records borrow from the input; fields and writer output borrow from caller buffers
- examples: `conformance/native/pass/std-csv.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let input: Span<u8> = "name,quote\nAda,\"a,b\"\n"
    let record: Maybe<Span<u8>> = std.csv.record(input, 1_usize)
    var field_buf: [16]u8 = [0_u8; 16]
    var quote: Maybe<Span<u8>> = null
    if record.has {
        quote = std.csv.field(field_buf, record.value, 1_usize)
    }
    if std.csv.valid(input) && quote.has && std.mem.eql(quote.value, "a,b") {
        check world.out.write("csv ok\n")
    }
}
```

## Design Notes

CSV helpers follow the common RFC 4180 field rules: comma-separated fields,
quoted fields, doubled quotes inside quoted fields, and CRLF or LF record
separators. Quoted fields may contain newlines.

The writer surface is fixed-arity. Use `writeField` when a custom writer loop
is needed.
