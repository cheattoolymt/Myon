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
| Step 17 | async/await（実験的・疑似非同期 → Phase5 で協調的イベントループへ本格化） | ✅ |
| Step 18 | 総合テスト（回帰テストとして `tests/cases/` に集約） | ✅ |

### Phase 2（追加実装 P1〜P7）

| 項目 | 内容 | 状態 |
|---|---|---|
| P1 | `()`/`[]` 内の改行を文区切りにしない（複数行の引数リスト） | ✅ 実装 |
| P2 | `myon.not` の前置専用ルールをドキュメントに明記 | ✅ ドキュメント |
| P3 | `myon` 単体起動時の対話式実行（REPL） | ✅ 実装 |
| P4 | ファイル I/O（`myon.file.read`/`write`/`append`/`exists`） | ✅ 実装 |
| P5 | エラーメッセージの詳細化（列番号・ソース抜粋・`^` マーカー） | ✅ 実装 |
| P6 | `myon.async`/`myon.await` の実行モデル → 当初は疑似非同期に確定、Phase5 で協調的イベントループへ本格化（OSスレッド化は引き続き不採用） | ✅ 設計確定 |
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

### Phase 4（`myon.array` / `myon.map` メソッド拡充、`myon.math` 整数演算、`myon.time` / `myon.random` 新設）

コレクション型のメソッドを実用的なラインナップへ拡充し、大きな整数の冪乗を正確に
計算する整数演算と、時刻取得・乱数生成の新規モジュールを追加しました。

| ステップ | 内容 | 状態 |
|---|---|---|
| Step 1 | `myon.array` に `sort` / `sort_desc` / `reverse` / `contains` / `index_of` / `slice` を追加（`sort`系は破壊的、`qsort`＋型判定`compar`、`slice`は範囲外を`error`） | ✅ 実装 |
| Step 2 | `myon.map` に `keys` / `values` / `length` を追加（`MapEntry`連結リストを走査、順序保証なし） | ✅ 実装 |
| Step 3 | `myon.array` に高階関数 `map` / `filter` / `reduce` を追加（`myon.lambda`対応、既存`call_function`を再利用） | ✅ 実装 |
| Step 4 | `myon.math` に整数演算 `gcd` / `lcm` / `pow_int` を追加（繰り返し二乗法・オーバーフロー検出、`double`非経由で`2^62`も正確） | ✅ 実装 |
| Step 5 | 新規モジュール `myon.time`（`now` / `now_ms` / `sleep_ms`） | ✅ 実装 |
| Step 6 | 新規モジュール `myon.random`（`seed` / `int` / `float`、初回自動シード、`rand()`ベース） | ✅ 実装 |
| Step 7 | ドキュメント整備・回帰テスト追加 | ✅ 実装 |

`myon.array` は `sort` / `sort_desc` / `reverse`（破壊的）、`contains` / `index_of` /
`slice`（非破壊）、および `map` / `filter` / `reduce`（`myon.lambda` を取る高階関数）を
備えます。`myon.map` は `keys` / `values` / `length` で中身を列挙できます（順序は保証
しないため、必要なら返り配列を `sort()` してから使います）。`myon.math.pow_int` は
`double` を経由しないため `2^62`（`4611686018427387904`）のような大きな整数冪も桁落ち
なく計算でき、負指数・オーバーフローは `error` を返します。`myon.time` は UNIX エポック
秒/ミリ秒の取得とスリープ、`myon.random` は `srand`/`rand` を薄くラップした乱数生成を
提供します（**暗号学的に安全ではありません**）。回帰テストは
`tests/cases/p4_array_methods`（sort/reverse/contains/index_of/slice）、
`p4_array_higher_order`（map/filter/reduce）、`p4_map_methods`（keys/values/length、
順序非依存）、`p4_math_integer`（gcd/lcm/pow_int）、`p4_time_basic`・`p4_random_basic`
（時刻・乱数は不変条件のみ検証し `.out` を決定的化）でカバーします。全シグネチャは
仕様書 [`docs/myon_spec.md`](docs/myon_spec.md) の「7.1 配列メソッド」「14.2 辞書/マップ型」
「10.4 myon.math」「10.5 myon.time」「10.6 myon.random」各節を参照してください。

### Phase 4.1（GUI/ゲーム制作向け FFI 拡張 — 型付き書き込み・構造体レイアウト DSL・配列一括読み書き・コールバック）

SDL/OpenGL のような GUI・ゲームライブラリを実用的に FFI から叩けるよう、Phase3.1
までの「単純な値渡し＋バイト列」を4系統で拡張しました。いずれも C 側のヘッダは
一切読まず、Myon 側が申告したレイアウト・型をそのまま信じる方針（Phase3 以来）です。

| ステップ | 内容 | 状態 |
|---|---|---|
| Step 1 | 型付きメモリ書き込み `myon.ffi.write_i64`/`write_i32`/`write_f64`/`write_f32`（NULバイトを含む値も欠落なく書ける、`ffi_mem_write_*`） | ✅ 実装 |
| Step 2 | 構造体レイアウト DSL `myon.ffi.struct_def`/`struct_alloc`/`struct_write`/`struct_read`（自然アライメント・末尾パディング自動計算、`ffi_struct_define`ほか） | ✅ 実装 |
| Step 3 | 配列一括読み書き `myon.ffi.write_array_*`/`read_array_*`（i32/i64/f32/f64、`ffi_mem_write_array_*`/`ffi_mem_read_array_*`） | ✅ 実装 |
| Step 4 | コールバック関数ポインタ（限定版）`myon.ffi.make_callback`/`free_callback`（libffi 不使用、静的トランポリン、`src/ffi_callback.{h,c}`） | ✅ 実装 |
| Step 5 | 仕様書 10.3.2 節を追加 | ✅ 実装 |
| Step 6 | 回帰テスト追加（`p41_*`、コールバックは fixture の `.so` を事前ビルド） | ✅ 実装 |
| Step 7 | README 更新・`SDL_Rect` サンプル追加 | ✅ 実装 |

型付き書き込みは値の生バイト列をリトルエンディアンで直接書き込むため、`int` の
エンコードに NUL バイトが混ざっても途切れません（`write_bytes` の制約を解消）。
構造体レイアウト DSL は `["i32","i32","i32","i32"]` のような型リストから各フィールドの
オフセットと合計サイズを自動計算し、C のデフォルト構造体パディング（自然アライメント＋
末尾パディング）に従います（`SDL_Rect` はパディングなしで16バイト、`i32`/`i64` 混在は
自動でパディング）。`struct_read` の戻り値スカラー型はフィールド型から実行時に決まります
（Myon のタプル返却が型なし配列パックのため単一関数で自然に扱える、という設計判断）。
配列一括読み書きは頂点/色配列のような同型データを1回の呼び出しでやり取りします。
コールバックは **引数最大4個・int64/ptr のみ・戻り値 int64 のみ・同時16スロット** の
限定スコープで、libffi の closure を使わず静的トランポリン関数群を介して Myon 関数を
呼び返します（シングルスレッド前提）。

回帰テストは `tests/cases/p41_ffi_write_typed`（型付き書き込みの roundtrip、NUL バイトを
含む値を明示）、`p41_ffi_struct_dsl`（i32 のみ／i32・i64 混在パディング）、
`p41_ffi_array_bulk`（配列 roundtrip＋範囲外エラー）、`p41_ffi_callback`（C→Myon コール
バック、スロット再利用）でカバーします。コールバックテストは CI 再現性のため
`tests/fixtures/ffi_callback_test.c` を `tests/run_tests.sh` が実行前に `.so` へビルド
します。構造体 DSL を使った `SDL_Rect` の組み立て例は `examples/ffi_sdl_rect.myon` に
あり、SDL2 が無い環境でも構造体の組み立て・確認部分は独立して動作します。全シグネチャ
と設計判断は仕様書 [`docs/myon_spec.md`](docs/myon_spec.md) の「10.3.2 GUI/ゲーム制作
向け FFI 拡張（Phase4.1）」節を参照してください。

### Phase 5（async/await 本格化 — 協調的イベントループ・`myon.net`・`myon.http`）

Step17 の「疑似非同期（呼び出し即同期実行）」を廃止し、シングルスレッドの
**協調的イベントループ**（`ucontext` ベースのコルーチン、`src/event_loop.{h,c}`）へ
置き換えました。あわせて低水準ソケット `myon.net`（TCP/UDP）と、その上に構築した
簡易 HTTP モジュール `myon.http`（静的配信／ルーティングサーバー＋自前 TCP クライアント）
を追加しています。本物の OS スレッド化は Phase2 P6 の判断どおり採用せず（`refcount` の
スレッドセーフ化が必要で侵襲が大きすぎるため）、切り替えは `await`／I/O 待ち地点のみで
起こる協調的マルチタスク（Python asyncio / JS async-await と同じモデル）です。

| ステップ | 内容 | 状態 |
|---|---|---|
| Step 1 | イベントループ基盤（`ucontext` コルーチン・`select(2)` 多重化・spawn/await/sleep/IO待ち、`src/event_loop.{h,c}`） | ✅ 実装 |
| Step 2 | インタプリタ統合（`TYPE_TASK`/`OBJ_TASK`、`is_async` 関数が Task 値を返す、`EXPR_AWAIT` の本実装、トップレベルも1タスク化） | ✅ 実装 |
| Step 3 | `myon.time.sleep_ms` の非同期対応（コルーチン内は `event_loop_sleep_ms`、同期文脈はブロッキングにフォールバック） | ✅ 実装 |
| Step 4 | 新規モジュール `myon.net`（`tcp_socket`/`udp_socket`/`bind`/`listen`/`local_port`/`accept`/`connect`/`send`/`recv`/`send_to`/`recv_from`/`close`、ノンブロッキング＋協調待機、`src/net.{h,c}`） | ✅ 実装 |
| Step 5 | 新規モジュール `myon.http`（`serve_static`/`serve`/`get`/`post`、HTTP/1.0・自前 TCP クライアント、`src/http.{h,c}`） | ✅ 実装 |
| Step 6 | 仕様書更新（14.9 実行モデル書き換え・10.7 `myon.net`・10.8 `myon.http`・15 Open Questions） | ✅ 実装 |
| Step 7 | 回帰テスト追加（`p5_async_order`／`p5_net_tcp_echo`／`p5_net_udp_echo`／`p5_http_serve_static`、いずれも自己完結でCI非依存） | ✅ 実装 |
| Step 8 | README 更新・サンプル追加（`http_static_server`／`http_router_server`／`net_game_echo`） | ✅ 実装 |

各ソケットは常にノンブロッキング（`O_NONBLOCK`）で作られ、`myon.async` 関数の中から
呼ぶと would-block 時に自動でイベントループへ制御を譲り、他のタスクへ切り替わります。
`myon.async` を使わないトップレベル（同期文脈）から呼んだ場合は、単一 fd の `select()`
による同期待機にフォールバックするため、後方互換的な単純スクリプトでもそのまま使えます。
`myon.http.serve_static`/`serve` の accept ループはデーモンタスクとしてマークされ、
プログラム終了時のドレイン処理では無視されるため、サーバーを起動しつつ別タスクで
クライアント処理をして終了する自己完結スクリプトがハングせず終了します。

回帰テストは `tests/cases/p5_async_order`（3タスクが sleep 時間の短い順＝spawn 順とは
異なる順で完了することを検証）、`p5_net_tcp_echo` / `p5_net_udp_echo`（同一プロセス内で
サーバー役・クライアント役を両方 `myon.async` タスクとして立て、ポート0＋`local_port`
で衝突を避けつつ1往復のエコーを検証）、`p5_http_serve_static`（`serve_static` を
デーモンタスクとして起動しつつ `myon.http.get` で取得、ステータス・本文を検証）で
カバーします。いずれも外部プロセス（`nc`/`curl`）に依存しない自己完結テストです。
サンプルは `examples/http_static_server.myon`（`python -m http.server` 相当の静的配信）、
`examples/http_router_server.myon`（パスに応じて分岐するルーティング＋404）、
`examples/net_game_echo.myon`（UDP による1対1の座標 echo デモ）に置いてあります
（常駐サーバー／ネットワークI/Oを伴うため回帰テストには含めません）。全シグネチャと
実行モデルの詳細は仕様書 [`docs/myon_spec.md`](docs/myon_spec.md) の「14.9 並行処理・
非同期処理」「10.7 ネットワーク myon.net」「10.8 HTTP myon.http」各節を参照してください。

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
