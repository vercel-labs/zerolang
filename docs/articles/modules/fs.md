## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.fs.read(path, buf)` | `usize` | Reads bytes from a hosted path into a caller-provided `MutSpan<u8>` buffer. |
| `std.fs.write(path, bytes)` | `usize` | Writes bytes to a hosted path and returns the byte count. |
| `std.fs.host()` | `Fs` | Creates the hosted filesystem capability. |
| `std.fs.open(fs, path)` | `Maybe<owned<File>>` | Opens a file and returns `null` when unavailable. |
| `std.fs.openOrRaise(fs, path)` | `owned<File>` | Opens a file or raises `{ NotFound, TooLarge, Io }`. |
| `std.fs.create(fs, path)` | `Maybe<owned<File>>` | Creates a file and returns `null` when unavailable. |
| `std.fs.createOrRaise(fs, path)` | `owned<File>` | Creates a file or raises `{ NotFound, TooLarge, Io }`. |
| `std.fs.readOrRaise(&mut file, buf)` | `usize` | Reads into caller storage or raises. |
| `std.fs.writeAll(&mut file, bytes)` | `Bool` | Writes bytes to an owned file handle. |
| `std.fs.writeAllOrRaise(&mut file, bytes)` | `Void` | Writes all bytes or raises. |
| `std.fs.fileLen(&mut file)` | `Maybe<usize>` | Reports file length when available. |
| `std.fs.fileLenOrRaise(&mut file)` | `usize` | Reports the file length or raises. |
| `std.fs.readAll(alloc, fs, path, limit)` | `Maybe<owned<ByteBuf>>` | Reads through an explicit allocator and size limit. |
| `std.fs.readAllOrRaise(alloc, fs, path, limit)` | `owned<ByteBuf>` | Reads through an explicit allocator and size limit. |
| `std.fs.readBytes(path, buf)` | `Maybe<usize>` | Reads bytes into caller storage. |
| `std.fs.writeBytes(path, bytes)` | `Maybe<usize>` | Writes byte spans to a hosted path. |
| `std.fs.exists(path)` | `Bool` | Checks whether a hosted path exists. |
| `std.fs.isDir(path)` | `Bool` | Checks whether a hosted path is a directory. |
| `std.fs.makeDir(path)` | `Bool` | Creates a hosted directory. |
| `std.fs.removeDir(path)` | `Bool` | Removes a hosted directory. |
| `std.fs.remove(path)` | `Bool` | Removes a hosted file path. |
| `std.fs.rename(old, new)` | `Bool` | Renames a hosted file path. |
| `std.fs.dirEntryCount(path)` | `Maybe<usize>` | Counts entries in a hosted directory. |
| `std.fs.tempName(buffer, prefix)` | `Maybe<String>` | Writes a temporary path into caller storage. |
| `std.fs.atomicWrite(path, temp, bytes)` | `Bool` | Writes through a caller-provided temporary path and renames. |
| `std.fs.close(&mut file)` | `Void` | Closes an owned file handle explicitly; remaining owned files are cleaned up deterministically. |

Current limits:

- Richer permissions and platform-specific file modes.
- Directory walking.
- Async or nonblocking I/O.

## Example

```zero
pub fun main(world: World) -> Void raises { NotFound, TooLarge, Io } {
    let fs = std.fs.host()
    let mut file: owned<File> = check std.fs.createOrRaise(fs, ".zero/out/example.txt")
    check std.fs.writeAllOrRaise(&mut file, std.mem.span("hello\n"))
    let len = check std.fs.fileLenOrRaise(&mut file)
    std.fs.close(&mut file)
    if len == 6 && std.fs.exists(".zero/out/example.txt") {
        if std.fs.rename(".zero/out/example.txt", ".zero/out/example-renamed.txt") {
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
