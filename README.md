# Myon

半分ネタ、半分実用(？)

**Myon** は型に厳密なプログラミング言語です。言語仕様は [`docs/myon_spec.md`](docs/myon_spec.md) を参照してください。

## 実装状況

`make test` により、下表の各ステップ・項目に対応する回帰テスト（計 27 ケース）がすべてパスします。

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
