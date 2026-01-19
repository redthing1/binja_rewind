
# binja_rewind

cross-platform time-travel debugging for binaryninja, powered by [w1tn3ss](https://github.com/redthing1/w1tn3ss/)'s `w1rewind`/`w1replay` record-and-replay engine.

## build

```sh
# grab submodules
git submodule update --init --recursive
# configure
cmake -G Ninja -B build-release -DBINJA_API_VERSION=<commit_hash> -DBINJA_QT_VERSION=<qt_ver>
# build
cmake --build build-release --parallel
```
