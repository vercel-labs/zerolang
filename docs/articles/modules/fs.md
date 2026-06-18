## When To Use std.fs

In Zerolang, use `std.fs` for hosted file reads, writes, existence checks, copies, renames,
and explicit file-resource cleanup.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.fs.read(path, buf)` | `usize` | Reads bytes from a hosted path into a caller-provided `MutSpan<u8>` buffer. |
| `std.fs.write(path, bytes)` | `usize` | Writes bytes to a hosted path and returns the byte count. |
| `std.fs.host()` | `Fs` | Creates the hosted filesystem capability. |
| `std.fs.open(fs, path)` | `Maybe<owned<File>>` | Opens a file and returns `null` when unavailable. |
| `std.fs.openOrRaise(fs, path)` | `owned<File>` | Opens a file or raises `raises [NotFound, TooLarge, Io]`. |
| `std.fs.create(fs, path)` | `Maybe<owned<File>>` | Creates a file and returns `null` when unavailable. |
| `std.fs.createOrRaise(fs, path)` | `owned<File>` | Creates a file or raises `raises [NotFound, TooLarge, Io]`. |
| `std.fs.readOrRaise(&mut file, buf)` | `usize` | Reads into caller storage or raises. |
| `std.fs.writeAll(&mut file, bytes)` | `Bool` | Writes bytes to an owned file handle. |
| `std.fs.writeAllOrRaise(&mut file, bytes)` | `Void` | Writes all bytes or raises. |
| `std.fs.fileLen(&mut file)` | `Maybe<usize>` | Reports file length when available. |
| `std.fs.fileLenOrRaise(&mut file)` | `usize` | Reports the file length or raises. |
| `std.fs.fileSize(fs, path)` | `Maybe<usize>` | Opens a hosted path through `fs` and reports file length when available. |
| `std.fs.readAll(alloc, fs, path, limit)` | `Maybe<owned<ByteBuf>>` | Reads through an explicit allocator and size limit. |
| `std.fs.readAllOrRaise(alloc, fs, path, limit)` | `owned<ByteBuf>` | Reads through an explicit allocator and size limit. |
| `std.fs.readBytes(path, buf)` | `Maybe<usize>` | Fills caller storage and returns the total file size; a value above `len(buf)` means the buffer holds only the first `len(buf)` bytes. |
| `std.fs.readBytesAt(path, offset, buf)` | `Maybe<usize>` | Fills caller storage starting at a byte offset and returns the total file size, so bounded buffers can process larger files in chunks. |
| `std.fs.writeBytes(path, bytes)` | `Maybe<usize>` | Writes byte spans to a hosted path. |
| `std.fs.appendBytes(path, bytes)` | `Maybe<usize>` | Appends byte spans to a hosted path, creating the file when missing. |
| `std.fs.exists(path)` | `Bool` | Checks whether a hosted path exists. |
| `std.fs.isFile(path)` | `Bool` | Checks whether a hosted path opens and reports a file length. |
| `std.fs.isDir(path)` | `Bool` | Checks whether a hosted path is a directory. |
| `std.fs.makeDir(path)` | `Bool` | Creates a hosted directory. |
| `std.fs.ensureDir(path)` | `Bool` | Succeeds when a hosted directory already exists or can be created. |
| `std.fs.removeDir(path)` | `Bool` | Removes a hosted directory. |
| `std.fs.remove(path)` | `Bool` | Removes a hosted file path. |
| `std.fs.rename(old, new)` | `Bool` | Renames a hosted file path. |
| `std.fs.dirEntryCount(path)` | `Maybe<usize>` | Counts entries in a hosted directory. |
| `std.fs.dirEntryName(buffer, path, index)` | `Maybe<Span<u8>>` | Writes one hosted directory entry name into caller storage. |
| `std.fs.tempName(buffer, prefix)` | `Maybe<String>` | Writes a temporary path into caller storage. |
| `std.fs.atomicWrite(path, temp, bytes)` | `Bool` | Writes through a caller-provided temporary path and renames. |
| `std.fs.close(&mut file)` | `Void` | Closes an owned file handle explicitly; remaining owned files are cleaned up deterministically. |
| `std.fs.readFile(fs, path, buffer)` | `Maybe<usize>` | Opens, fills caller storage, and closes through explicit `Fs`; returns the total file size, so a value above `len(buffer)` signals truncation. |
| `std.fs.writeFile(fs, path, bytes)` | `Bool` | Creates, writes all bytes, and closes through explicit `Fs`. |
| `std.fs.appendFile(fs, path, bytes)` | `Bool` | Opens, appends all bytes, and closes through explicit `Fs`. |
| `std.fs.readFileBytes(fs, path, buffer)` | `Maybe<Span<u8>>` | Opens, reads a full file, closes it, and returns the live prefix of caller storage; `null` when the file exceeds the buffer. |
| `std.fs.readFileEquals(fs, path, buffer, expected)` | `Bool` | Reads a full file through caller storage and compares the bytes with an expected span. |
| `std.fs.copyFile(from, to, buffer)` | `Bool` | Copies a hosted file through caller-provided scratch storage. |

Current limits:

- Richer permissions and platform-specific file modes.
- Recursive directory walking helpers.
- Async or nonblocking I/O.

## Example

```zero
pub fn main(world: World) -> Void raises [NotFound, TooLarge, Io] {
    let fs: Fs = std.fs.host()
    var buf: [32]u8 = [0_u8; 32]
    if std.fs.ensureDir(".zero/out") && std.fs.writeFile(fs, ".zero/out/example.txt", "hello\n") {
        let bytes: Maybe<Span<u8>> = std.fs.readFileBytes(fs, ".zero/out/example.txt", buf)
        let size: Maybe<usize> = std.fs.fileSize(fs, ".zero/out/example.txt")
        if bytes.has && size.has && size.value == 6 && std.fs.isFile(".zero/out/example.txt") && std.fs.readFileEquals(fs, ".zero/out/example.txt", buf, "hello\n") && std.fs.rename(".zero/out/example.txt", ".zero/out/example-renamed.txt") {
            if std.fs.remove(".zero/out/example-renamed.txt") {
                check world.out.write("fs ok\n")
            }
        }
    }
}
```

## Design Notes

The path helpers are a small current API, not a hidden global filesystem.

Stable file APIs make effects, ownership, and cleanup visible through
capabilities.

Hosted filesystem APIs are denied on non-host targets with `TAR002`.
Target-neutral packages should keep filesystem code outside their cross-target
entry point.
