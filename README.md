# Myon

半分ネタ、半分実用(？)

**Myon** は型に厳密なプログラミング言語です。言語仕様は [`docs/spec.md`](docs/spec.md) を参照してください。
本リポジトリには C 言語による処理系（インタプリタ）を、
[`myon_implementation_steps.md`](myon_implementation_steps.md) のステップに沿って段階的に実装しています。

## 実装状況

[`myon_implementation_steps.md`](myon_implementation_steps.md) の全ステップ（Step 0〜18）を実装済みです。
`make test` により、下表の各ステップに対応する回帰テスト（計 24 ケース）がすべてパスします。

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
  interpreter.{h,c}  ツリーウォーク型インタプリタ（Step 4〜17）
  common.{h,c}       共通ユーティリティ（メモリ確保・文字列複製）
  main.c             エントリポイント
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
