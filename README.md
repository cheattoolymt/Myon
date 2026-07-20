# Myon

半分ネタ、半分実用(？)

**Myon** は型に厳密なプログラミング言語です。言語仕様は [`docs/myon_spec.md`](docs/myon_spec.md) を参照してください。

## 実装状況

`make test` により、下表の各ステップ・項目に対応する回帰テスト（計 30 ケース）がすべてパスします。

### Step 0〜18（コア実装）

| ステップ | 内容 | 状態 |
|---|---|---|
| Step 0 | プロジェクト準備（Makefile・構成・LICENSE） | ✅ |
| Step 1 | 字句解析器（Lexer） | ✅ |
| Step 2 | AST 定義 | ✅ |
| Step 3 | パーサー（変数代入・`myon.print`・式の優先順位） | ✅ |
| Step 4 | 型システムの基礎（型チェック・キャスト・`myon.nil`） | ✅ |
| Step 5 | 制御構文（if/elif/else・while・for・break/continue） | ✅ |
| Step 6 | 関数（`myon.func`・複数戻り値・`error`・`myon.lambda`） | ✅ |
| Step 7 | 配列（`myon.array`・push/pop/length・インデックスアクセス） | ✅ |
| Step 8 | スコープ規則と `myon.expose`（ブロックスコープ・シャドーイング禁止） | ✅ |
| Step 9 | 構造体（`myon.struct`・メソッド・`self`・`myon.extends`） | ✅ |
| Step 10 | 文字列補間（`"... {式} ..."`） | ✅ |
| Step 11 | モジュールシステム（`module myon.stdio` などの解決） | ✅ |
| Step 12 | 整数オーバーフローチェック（実行時エラー） | ✅ |
| Step 13 | 拡張リテラル（16進/8進/指数表記）と辞書型（`myon.map`） | ✅ |
| Step 14 | 型推論（単一リテラル代入に限り型注釈省略を許可） | ✅ |
| Step 15 | ジェネリクス（`myon.struct Box<T>` / `myon.func f<T>`） | ✅ |
| Step 16 | 標準ライブラリ（`myon.math` / `myon.string`） | ✅ |
| Step 17 | async/await（実験的・疑似非同期） | ✅ |
| Step 18 | 総合テスト（回帰テストとして `tests/cases/` に集約） | ✅ |

### Phase 2（追加実装 P1〜P7）

| 項目 | 内容 | 状態 |
|---|---|---|
| P1 | `()`/`[]` 内の改行を文区切りにしない（複数行の引数リスト） | ✅ 実装 |
| P2 | `myon.not` の前置専用ルールをドキュメントに明記 | ✅ ドキュメント |
| P3 | `myon` 単体起動時の対話式実行（REPL） | ✅ 実装 |
| P4 | ファイル I/O（`myon.file.read`/`write`/`append`/`exists`） | ✅ 実装 |
| P5 | エラーメッセージの詳細化（列番号・ソース抜粋・`^` マーカー） | ✅ 実装 |
| P6 | `myon.async`/`myon.await` の実行モデル → 疑似非同期に確定 | ✅ 設計確定 |
| P7 | ジェネリクスの型制約 → 導入しないことに確定 | ✅ 設計確定 |

P1・P3・P4・P5 には対応するテストケース（`tests/cases/p1_multiline_args`,
`tests/cases/p4_file_io`, `tests/cases/p5_error_detail` ほか）を追加しています。
P2・P6・P7 は仕様書（`docs/myon_spec.md`）の更新で完了です。

### Phase 3（C FFI — 外部共有ライブラリ呼び出し）

| ステップ | 内容 | 状態 |
|---|---|---|
| Step 1 | プラットフォーム抽象化層（`dlopen`/`dlsym`/`dlclose` ラップ、`src/ffi_platform.{h,c}`） | ✅ 実装 |
| Step 2 | FFI 型・ハンドル管理レイヤ（`src/ffi.{h,c}`） | ✅ 実装 |
| Step 3 | 呼び出しテーブル（libffi 不使用、引数パターン別 C ラッパー、`src/ffi_call.{h,c}`） | ✅ 実装 |
| Step 4 | Myon 言語結合（`module myon.ffi`：`load`/`close`/`call_i`/`call_d`/`call_p`/`call_v`） | ✅ 実装 |
| Step 5 | 回帰テスト・ドキュメント仕上げ | ✅ 実装 |

### Phase 3.1（FFI 拡張 — メモリ確保・文字列デリファレンス・バッファ引数）

| ステップ | 内容 | 状態 |
|---|---|---|
| Step 1 | メモリ確保・解放プリミティブ（`myon.ffi.alloc` / `myon.ffi.free`、`src/ffi.{h,c}`） | ✅ 実装 |
| Step 2 | sig シグネチャへの `'b'`（バッファ／ブロックID引数）追加、out 引数対応 | ✅ 実装 |
| Step 3 | 文字列デリファレンス（`myon.ffi.read_cstr`、`ffi_read_cstring`） | ✅ 実装 |
| Step 4 | バイト列読み書き（`myon.ffi.write_bytes` / `read_bytes`）＋ `read_i64` ヘルパー | ✅ 実装 |
| Step 5 | 安全性の見直しとドキュメント整備 | ✅ 実装 |
| Step 6 | 実地検証（`sqlite3_open`）とテスト追加 | ✅ 実装 |

Phase 3.1 では、Myon 側から C に渡すための生メモリ領域を確保・解放できる
`myon.ffi.alloc` / `myon.ffi.free`、確保したブロックを sig `'b'` で C 関数へ渡す
仕組み（`sqlite3_open` のような「出力引数」対応）、`const char*` の戻り値を Myon の
`str` に読み出す `myon.ffi.read_cstr`、確保ブロックへのバイト列読み書き
`myon.ffi.write_bytes` / `myon.ffi.read_bytes`、および出力引数として書き込まれた
ポインタ値を取り出す `myon.ffi.read_i64`（リトルエンディアン int64）を追加しました。
回帰テストは環境非依存の `libz` を用い、文字列デリファレンス（`tests/cases/p31_ffi_read_cstr`）、
バイト列書き込み＋`crc32`（`tests/cases/p31_ffi_bytes_crc32`）、`read_i64`
（`tests/cases/p31_ffi_read_i64`）、純粋なメモリ管理（`tests/cases/p31_ffi_alloc_free`）
をカバーします。文字列を返す C 関数の例は `examples/ffi_zlib_version.myon`、出力引数
（`sqlite3_open`）の例は `examples/ffi_sqlite_open.myon` にあります（後者は `libsqlite3` が
必要なため `examples/` のみ）。

> str型はNUL終端の `char*` として表現されるため、`write_bytes` / `read_bytes` は
> NULバイトを含むデータでは途切れる（バイナリセーフではない）という既知の制約が
> あります。詳細は仕様書 10.3.1 節を参照してください。

`module myon.ffi` を宣言すると、ビルド済みの共有ライブラリ（Linux の `.so` など）に
含まれる C 関数を実行時に呼び出せます。対応する値の型は `int` / `float` / ポインタ（`int`
として表現）/ `str` の4種類で、構造体の値渡し・値返しは対象外です。対応 OS は Linux
（macOS / Windows は `myon.ffi.load` 時に「未対応」の `error` を返すスタブ）。詳細と制約は
仕様書 [`docs/myon_spec.md`](docs/myon_spec.md) の「10.3 C FFI」節を参照してください。
テストは環境非依存の `libm`（数学ライブラリ）を用います（`tests/cases/p_ffi_basic`,
`tests/cases/p_ffi_load_fail`, `tests/cases/p_ffi_close`）。SDL2 を使った非対話デモは
`examples/ffi_sdl_window.myon` に置いてあります（回帰テストには含めません）。

### Phase 3.5（`myon.math` / `myon.string` 拡張）

Step 16 で最小実装した標準ライブラリ `myon.math` / `myon.string` を、実用的な
ラインナップへ拡張・修正しました。

| ステップ | 内容 | 状態 |
|---|---|---|
| Step 1 | 数値型保持ルールの統一（`max`/`min`/`floor`/`ceil` を int/float パスへ分岐、`both_int` ヘルパー、2^53超の int64 を正確に扱う） | ✅ 実装 |
| Step 2 | `myon.string.length` を UTF-8 文字数（コードポイント数）へ修正、`byte_length` を新設（`utf8_char_count`） | ✅ 実装 |
| Step 3 | `myon.math` フルセット（三角/逆三角/`atan2`・`log`/`log2`/`log10`/`exp`・`round`/`trunc`/`mod`/`sign`/`clamp`・`pi`/`e`） | ✅ 実装 |
| Step 4 | `myon.string` フルセット（`substring`/`split`/`join`/`trim`/`replace`/`index_of`/`starts_with`/`ends_with`/`repeat`/`to_int`/`to_float`/`from_int`/`from_float`、`utf8_byte_offset`） | ✅ 実装 |
| Step 5 | 回帰テスト・ドキュメント整備 | ✅ 実装 |

数値関数は「全引数が int なら int、1つでも float なら float」という暗黙昇格ルールに
統一しました。整数のまま意味が保たれる関数（`abs`/`max`/`min`/`floor`/`ceil`/`round`/
`trunc`/`mod`/`sign`/`clamp`）は `double` を経由せず、`2^53` を超える `int64` 値でも
丸め誤差なく扱えます。文字列関数の `length` は **文字数**、`byte_length` は **バイト数**
を返し、`substring` / `index_of` など索引系は全て **文字数ベース**（UTF-8 マルチバイト
安全）です。`mod` / `substring` / `repeat` / `to_int` / `to_float` はゼロ除算・範囲外・
パース失敗を `(value, error)` の2値で返します。回帰テストは
`tests/cases/p35_math_int_precision`（境界値精度）、`p35_math_full`（数学フルセット）、
`p35_string_utf8_length`（文字数/バイト数）、`p35_string_full`（文字列フルセット）で
カバーします。全関数のシグネチャは仕様書
[`docs/myon_spec.md`](docs/myon_spec.md) の「10.4 標準ライブラリ myon.math /
myon.string（Phase3.5）」節にまとめてあります。

## ビルド

```sh
make
```

`myon` という実行ファイルが生成されます。

## 使い方

```sh
./myon examples/hello.myon          # プログラムを実行
./myon --tokens examples/hello.myon # トークン列を出力（Step 1 の確認用）
./myon --tokens -                   # 標準入力から読み込む
./myon                              # 引数なし: 対話式実行（REPL, P3）
```

### 対話モード（REPL, P3）

```sh
$ ./myon
Myon REPL. Type 'exit' or 'quit' to leave (Ctrl+D also works).
myon> x = 1
myon> myon.print(x)
1
myon> exit
```

入力が未完（`()`/`[]`/`{}` が閉じていない）の場合は継続プロンプト `...> ` を表示し、
定義した変数・関数・構造体はセッション終了まで保持されます。実行時エラーが起きても
REPL は終了せず、次の入力を受け付け続けます。

### ファイル I/O（P4）

`module myon.stdio` を宣言すると、`myon.file.read` / `myon.file.write` /
`myon.file.append` / `myon.file.exists` が使えます。読み書きの失敗は Rust/Go 風の
`error` 値として第2戻り値に返り、`myon.if err != myon.nil` で捕捉できます。

### エラーメッセージ（P5）

構文・実行時エラーは、行番号・列番号・該当行のソース抜粋・`^` マーカー付きで
表示されます。

```
myon: syntax error at line 5, column 17: expected ')' to close call (got an integer literal)
     5 | myon.print(1, 2 3)
       |                 ^
```

### 実行例

```sh
$ ./myon examples/hello.myon
Hello Worlddd! 人間!
```

## プロジェクト構成

```
src/
  token.{h,c}        トークン定義
  lexer.{h,c}        字句解析器（Step 1）
  types.{h,c}        型システム（Step 4）
  value.{h,c}        実行時の値表現
  ast.{h,c}          AST ノード定義（Step 2）
  parser.{h,c}       再帰下降パーサー（Step 3）
  env.{h,c}          変数スコープ環境
  interpreter.{h,c}  ツリーウォーク型インタプリタ（Step 4〜17・P4 ファイルI/O）
  common.{h,c}       共通ユーティリティ（メモリ確保・文字列複製）
  diag.{h,c}         診断ヘルパー（P5: ソース抜粋・列番号・トークン名変換）
  ffi_platform.{h,c} C FFI プラットフォーム抽象化層（dlopen/dlsym, Phase3 Step1）
  ffi.{h,c}          C FFI 型・ハンドル管理レイヤ（Phase3 Step2）
  ffi_call.{h,c}     C FFI 呼び出しディスパッチ（libffi 不使用, Phase3 Step3）
  main.c             エントリポイント（ファイル実行 / REPL, P3）
examples/            サンプルプログラム
tests/               回帰テスト（`make test`）
```

## テスト

```sh
make test
```

`tests/cases/` 以下の `*.myon` を実行し、`*.out`（期待出力）または
`*.err`（エラー終了を期待）と比較します。

## ライセンス

Apache License, Version 2.0. 詳細は [`LICENSE`](LICENSE) を参照してください。
