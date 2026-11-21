# Encrypted RAR support

karyo-archive uses native libarchive and distribution 7-Zip executables.
The optional karyo-archive-rar package adds the distribution 7zip-rar codec for
encrypted RAR data and headers. Its code/license remains in that external package.
Debian supplies it in non-free; Ubuntu supplies it in multiverse. Enable the
appropriate official distribution component yourself if it is unavailable.
Karyo packages never change the host's repository configuration.

The base application supports ZIP/7z creation and supported libarchive formats
without this addon. Complete protected-RAR integration is tested with the codec
installed. Missing engines produce an operation error rather than a false success.
