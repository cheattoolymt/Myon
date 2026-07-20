# Myon言語仕様書 v0.1

## 0. 概要

**Myon** は実用性を見据えた、型に厳密なプログラミング言語である。
`myon.` プレフィックスによる名前空間の明示、`str()`/`char()`/`int()` のような値コンストラクタ風の型構文、
そして独自のスコープ公開機構 `myon.expose` を特徴とする。

### (0.1and)0.2 設計方針まとめ

| 項目 | 方針 |
|---|---|
| プレフィックス | `myon.stdio` 等モジュール読み込み以外は原則 `myon.` を付与 |
| 型システム | 厳密（異なる型同士の演算はエラー） |
| コメント | `#`（改行不可） / `//`（改行可） / `/* */`（複数行） |
| 実装言語 | C（理想はアセンブリ） |
| エラー処理 | Rust/Go風、戻り値にエラーを含める |
| null | `myon.nil` はエラー値専用。通常変数はnil不可・初期化必須 |
| 整数オーバーフロー | 実行時エラー |

---

## 1. 字句規則

### 1.1 コメント

```myon
# 単一行コメント（改行不可）
// 単一行コメント（改行可能、JS的）
/* 
複数行コメント
何行でも書ける
*/
```

### 1.2 識別子

- 使用可能文字：英数字とアンダースコア（`_`）
- 数字始まりは不可
- 大文字・小文字を区別する

### 1.3 リテラル

| 型 | 例 | 備考 |
|---|---|---|
| int | `1`, `42`, `0x1F`, `0o17` | 10進数・16進数・8進数（14.1節参照） |
| float | `3.14`, `1.5e10` | 通常表記・指数表記（14.1節参照） |
| str | `str("人間")` | コンストラクタ経由 |
| char | `char("a")` | コンストラクタ経由、1文字 |
| bool | `true`, `false` | 直接代入可（int同様の扱い） |

### 1.4 文の区切り

- 改行で文を区切るのが基本
- セミコロン `;` を使えば1行に複数文を書ける

```myon
x = 1; y = 2; myon.print(x + y)
```

- ただし `()` / `[]` の内部にある改行は文の区切りとみなさない。
  これにより、関数呼び出しの引数リストや配列アクセスなどを複数行に
  分けて書ける（字句解析器が `(`・`[` の深さを数え、深度が0より大きい
  間は改行を読み飛ばす）。

```myon
hero = Hero(
    name = str("勇者"),
    hp = 80
)
```

- 一方、ブロック本体を囲む `{}` の内部では改行は引き続き文の区切りとして
  機能する（複数の文を並べるために改行が必要なため、`{}` は上記の深さ計算に
  含めない）。

### 1.5 予約語一覧

```
system module myon ret str char int float bool void error
myon.if myon.elif myon.else then
myon.while myon.for myon.in myon.break myon.continue range
myon.func myon.struct myon.array myon.map myon.print myon.input
myon.and myon.or myon.not myon.nil myon.expose
myon.lambda myon.extends self myon.async myon.await
true false as
```

---

## 2. 型システム

### 2.1 プリミティブ型

- `int`：整数。オーバーフロー時は実行時エラー
- `float`：浮動小数点数
- `str`：文字列（`str(...)` で構築）
- `char`：1文字（`char(...)` で構築）
- `bool`：真偽値（`true`/`false` を直接代入可）

### 2.2 型の厳密性

異なる型同士の演算は**すべてエラー**となる（例：`char + int` はコンパイル/実行時エラー）。

### 2.3 型変換（キャスト）

型コンストラクタが変換も兼ねる。

```myon
n = int("123")     // str → int
s = str(123)        // int → str
c = char(65)         // int → char
```

### 2.4 myon.nil

`myon.nil` は**エラー値専用**。通常の変数（int/str/bool等）に代入することはできず、
すべての変数は宣言時に初期化が必須。

```myon
x: int          // エラー：初期化なしの宣言は不可
x = 0            // OK

result, err = divide(10, 0)
myon.if err != myon.nil then { ... }   // myon.nilはerror型の文脈でのみ有効
```

---

## 3. 変数と代入

```myon
x = str("人間")
a = char("a")
b = 1
flag = true
```

### 3.1 演算子

**算術・比較**
```
+  -  *  /
==  !=  <  >  <=  >=
```

**論理演算子（プレフィックススタイル）**
```
myon.and  myon.or  myon.not
```

`myon.not` は前置専用の単項演算子である（EBNF: `not_expr = [ "myon.not" ], comparison ;`）。
必ずオペランドの前に置くこと。後置で書くと構文エラーになる。

```myon
// 正しい書き方（前置）
myon.if myon.not flag then { ... }

// 誤り（後置は構文エラーになる）
// myon.if flag myon.not then { ... }
```

**複合代入演算子**
```
+=  -=  *=  /=
```

### 3.2 演算子優先順位（上ほど優先度が高い）

| 優先度 | 演算子 | 種別 |
|---|---|---|
| 1 | `()` `[]` `.` | グループ化・添字・メンバアクセス |
| 2 | `myon.not` `-`（単項） | 単項演算子 |
| 3 | `*` `/` | 乗除 |
| 4 | `+` `-` | 加減 |
| 5 | `==` `!=` `<` `>` `<=` `>=` | 比較 |
| 6 | `myon.and` | 論理AND |
| 7 | `myon.or` | 論理OR |

---

## 4. 文字列補間

`{}` 内に任意の式を書ける（変数参照・計算・プロパティアクセス等）。

```myon
myon.print("Hello, {name}! Next year you'll be {age + 1}.")
myon.print("{p.name}'s age is {p.age}.")
```

---

## 5. 制御構文

### 5.1 条件分岐

```myon
myon.if a > b {
    myon.print("aがbよりデカい")
} myon.elif b > a then {
    myon.print("bがaよりデカい")
} myon.else {
    myon.print("しらね")
}
```

`then` は `myon.elif` の文法要素として残す（`myon.if`/`myon.else` には不要）。

### 5.2 繰り返し

```myon
myon.while a < b {
    myon.print(a)
    a = a + 1
    myon.if a == 5 then { myon.break }
}

myon.for i myon.in range(0, 10) {
    myon.if i == 3 then { myon.continue }
    myon.print(i)
}

myon.for item myon.in xs {
    myon.print(item)
}
```

---

## 6. 関数

### 6.1 基本構文

引数の型注釈は必須。戻り値は `ret 型` の後置スタイル。voidは明示的に `ret void`。

```myon
myon.func add(a: int, b: int) ret int {
    ret a + b
}

myon.func greet(name: str) ret void {
    myon.print("Hello, ", name, "!")
}
```

### 6.2 複数戻り値とエラー処理（Rust/Go風）

`error` はエラー専用の型で、`error(str)` の形でエラー値を生成する組み込みコンストラクタを持つ
（`str()`/`char()`/`int()` と同様の値コンストラクタパターン）。

```myon
myon.func divide(a: int, b: int) ret int, error {
    myon.if b == 0 then {
        ret 0, error("ゼロ除算")
    }
    ret a / b, myon.nil
}

result, err = divide(10, 0)
myon.if err != myon.nil then {
    myon.print("エラー: ", err)
} myon.else {
    myon.print("結果: ", result)
}
```

### 6.2.1 診断メッセージの形式（構文/実行時エラー）

構文エラー・実行時エラーは、gcc / Rust コンパイラ風の詳細な形式で報告する。

- **行番号と列番号**の両方を表示する（字句解析器が保持する `col` を活用）。
- **該当行のソースコード抜粋**と、エラー位置を指す `^` マーカーを表示する。
- 構文エラーでは、期待していたトークン種別を人間が読みやすい表現に変換して出す
  （例：`TOK_RPAREN` ではなく `')'`、`TOK_EOF` ではなく `end of file`）。

```
myon: syntax error at line 5, column 17: expected ')' to close call (got an integer literal)
     5 | myon.print(1, 2 3)
       |                 ^
```

実行時エラー（型不一致・範囲外アクセス・ゼロ除算・整数オーバーフロー等）でも、
発生行のソース抜粋を表示する。実行時は列情報を保持していないため、キャレットは
行頭を指すベストエフォートの位置となる。

```
myon: runtime error at line 4: division by zero
     4 | myon.print(a / b)
       | ^
```

### 6.3 ラムダ（無名関数）

`myon.lambda` で定義。外側スコープの変数を捕捉できる（通常のクロージャ）。

```myon
threshold = 10
isOver = myon.lambda(x: int) ret bool { ret x > threshold }
myon.print(isOver(15))   // true
```

---

## 7. 配列

`myon.array` 型として定義。可変長・同一型要素のみ。

```myon
xs = myon.array(int)
xs.push(1)
xs.push(2)
xs.push(3)

y = xs[0]              // []記法でアクセス
xs.pop()                 // 末尾を削除
len = xs.length()        // 要素数取得
xs.push("文字列")        // エラー：型不一致
```

---

## 8. 構造体（struct）

複数フィールド・異種混在データの受け皿。フィールド名指定でインスタンス生成。
メソッド定義・継承をサポートする。

```myon
myon.struct Person {
    name: str
    age: int

    myon.func greet() ret void {
        myon.print("Hello, my name is ", self.name)
    }
}

myon.struct Employee myon.extends Person {
    salary: int

    myon.func showSalary() ret void {
        myon.print(self.name, "'s salary: ", self.salary)
    }
}

p = Person(name=str("太郎"), age=20)
p.greet()

e = Employee(name=str("花子"), age=30, salary=5000)
e.greet()
e.showSalary()
```

---

## 9. スコープ規則

### 9.1 基本方針

ブロック（`{}`）スコープを基本としつつ、独自の `myon.expose` により
明示的に外側スコープへ変数を持ち出すことができる。

```myon
x = 1
{
    y = 2
    myon.expose y
}
myon.print(y)   // OK: exposeされたので見える
myon.print(x)   // OK: 元々外側
```

`myon.expose` を使わない限り、ブロックを抜けた変数は消える。

### 9.2 シャドーイング

**禁止**。同名変数の再定義はエラーとなる。

```myon
x = 1
{
    x = 2   // エラー：同名変数の再定義
}
```

---

## 10. 入出力

### 10.1 標準入出力

```myon
name = myon.input("お名前は？")   // str型で受け取る
myon.print("Hello Worlddd! ", x + "!")
```

### 10.2 ファイルI/O

`module myon.stdio` を宣言すると、以下のファイル操作関数が利用できる。
読み書きに失敗した場合（ファイル不在・権限不足など）は、`runtime_error` で
プロセスを終了させるのではなく、6.2節のエラー処理方式（Rust/Go 風、`error(...)`）
に従って第2戻り値として `error` 値を返す。呼び出し側は `myon.if err != myon.nil`
でこれを捕捉できる。

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `myon.file.read`   | `myon.file.read(path: str) ret str, error`            | ファイル全体を `str` として読み込む |
| `myon.file.write`  | `myon.file.write(path: str, content: str) ret bool, error`  | 上書き書き込み（成功時 `true`） |
| `myon.file.append` | `myon.file.append(path: str, content: str) ret bool, error` | 末尾に追記（成功時 `true`） |
| `myon.file.exists` | `myon.file.exists(path: str) ret bool`                | 存在確認（エラーは返さない） |

```myon
module myon.stdio

path = "/tmp/greeting.txt"
ok, werr = myon.file.write(path, "line1")
myon.if werr != myon.nil then { myon.print("書き込み失敗") }

aok, aerr = myon.file.append(path, "-line2")

content, rerr = myon.file.read(path)
myon.if rerr != myon.nil then {
    myon.print("読み込み失敗")
} myon.else {
    myon.print(content)          // line1-line2
}

myon.print(myon.file.exists(path))                       // true
myon.print(myon.file.exists("/tmp/does_not_exist.txt"))  // false
```

- 成功時の第2戻り値（`error`）は `myon.nil` になる。
- `NULL` パス等、明らかに未定義動作になりうる入力はガードする。
  パストラバーサル対策など高度な安全性は本フェーズの対象外。

---

### 10.3 C FFI（外部共有ライブラリ呼び出し, Phase3）

`module myon.ffi` を宣言すると、ビルド済みの共有ライブラリ
（Linux: `.so` / macOS: `.dylib` / Windows: `.dll`）に含まれるC関数を
実行時に読み込んで直接呼び出せる。内部的には `dlopen`/`dlsym`/`dlclose`
相当の仕組みを使う（`libffi` などの外部依存は持たない）。

失敗（ライブラリ不在・シンボル未検出・型不一致など）は 6.2 節の
エラー処理方式に従い、第2戻り値として `error` 値を返す。

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `myon.ffi.load`   | `myon.ffi.load(path: str) ret int, error`             | 共有ライブラリを読み込み、ハンドルID（`int`）を返す |
| `myon.ffi.close`  | `myon.ffi.close(handle: int) ret bool, error`         | ハンドルを閉じる（成功時 `true`） |
| `myon.ffi.call_i` | `myon.ffi.call_i(handle: int, name: str, sig: str, ...) ret int, error`   | `int` を返すC関数を呼ぶ |
| `myon.ffi.call_d` | `myon.ffi.call_d(handle: int, name: str, sig: str, ...) ret float, error` | `double` を返すC関数を呼ぶ |
| `myon.ffi.call_p` | `myon.ffi.call_p(handle: int, name: str, sig: str, ...) ret int, error`   | ポインタを返すC関数を呼ぶ（戻り値は `int` として表現） |
| `myon.ffi.call_v` | `myon.ffi.call_v(handle: int, name: str, sig: str, ...) ret bool, error`  | `void`（戻り値なし）のC関数を呼ぶ（成功時 `true`） |

**シグネチャ文字列（`sig`）の記法**

引数の型を1文字ずつ並べた文字列で指定する。

| 文字 | 対応するC型 | Myon側の型 |
|------|-------------|-----------|
| `i`  | `long long`（整数）  | `int` / `bool` |
| `d`  | `double`             | `float`（`int` も可、自動的に `double` へ変換） |
| `p`  | `void*`（不透明ポインタ） | `int`（ポインタ値をそのまま整数として保持） |
| `s`  | `const char*`        | `str` |
| `b`  | `void*`（Myonが確保したメモリブロックを指す, **Phase3.1**） | `int`（`myon.ffi.alloc` が返すブロックID） |

`'b'` と `'p'` の違い（Phase3.1）:

- `'p'` は「Cが返してきた生アドレス値をそのまま `int64` として受け渡すだけ」で、
  Myon側はそのブロックの中身を一切関知しない。
- `'b'` は「Myon側の `myon.ffi.alloc` で確保したブロックIDを渡すと、C呼び出しの
  直前にFFI層がそのブロックの実際のメモリアドレスに変換してからC関数に渡す」。
  これにより `sqlite3_open(path, &db)` のような「出力引数（ダブルポインタ）」に、
  Myonが確保した書き込み可能な領域のアドレスを安全に渡せる。
  `'b'` に `myon.ffi.alloc`（や `load`）が発行していない不正な値を渡した場合は
  `error` を返す（クラッシュを防ぐガード）。
- 引数の並び順の制約において、`'b'` は `'i'`/`'p'`/`'s'` と同じ「整数系」グループに
  属する（`double` 引数より前に置く）。

例: `"id"` は「`int` 引数を1つ、`double` 引数を1つ」の意味。
戻り値の型は関数名で分ける（`call_i` / `call_d` / `call_p` / `call_v`）。
引数は0〜6個までサポートする。`sig` が空文字列（`str("")`）の場合は
引数0個（可変長引数を一切渡さない）を意味する。

**対応する値の型と制約**

- 対応する値の型は `int64` / `double` / `ptr` / `string` の4種類のみ。
  構造体を値渡し・値返しする関数は本フェーズでは非対応。
- ポインタを介した受け渡しはサポートするが、呼び出し先が構造体の中身を
  書き込んで返す関数（例: `SDL_PollEvent(SDL_Event *event)`）は、その
  構造体メモリをMyon側で `ptr` として確保できないため扱えない。
  そのため本フェーズのFFIだけでは、ウィンドウを出して一定時間後に閉じる
  程度のデモは組めるが、`SDL_PollEvent` を使う一般的なイベントループ形式の
  GUIアプリは組めない。
- 引数の並びは「整数系（`i`/`p`/`s`）の引数がすべて先、`double`（`d`）の
  引数がすべて後」の順序のみサポートする（x86-64 System V ABI の
  レジスタ割り当てに対応。SDL2 の主要関数はこの並びに収まる）。
  それ以外の並び順や7引数以上は `error` を返す。
- **32bit幅のC引数への注意**: Myonの `int` は64bit だが、呼び出し先の
  C関数の引数が `int` / `unsigned int` / `Uint32` のような32bit幅である
  場合がある。呼ばれた側が下位32bitのみを参照するため、`0 〜 4294967295`
  の範囲で値を渡す限り問題は起きない。この範囲を超える値や負の値を渡すと
  C関数が意図しない値を受け取る可能性があるため、呼び出し側が正しい範囲の
  値を渡す責任を持つ（本フェーズでは自動的な範囲チェックは行わない）。

**対応プラットフォーム**

本フェーズの本命は Linux。macOS / Windows では `myon.ffi.load` を呼んだ
時点で「FFI is not supported on this platform yet (Phase3 stub)」という
`error` を返す（コンパイル自体は3OSとも通る）。

```myon
module myon.ffi

handle, err = myon.ffi.load(str("libm.so.6"))
myon.if err != myon.nil then { myon.print(str("load failed")) }

// sqrt(16.0) -> 4.0
result, cerr = myon.ffi.call_d(handle, str("sqrt"), str("d"), 16.0)
myon.print(result)                                 // 4

// pow(2.0, 10.0) -> 1024.0
p, perr = myon.ffi.call_d(handle, str("pow"), str("dd"), 2.0, 10.0)
myon.print(p)                                      // 1024

ok, clerr = myon.ffi.close(handle)
```

- 一度 `close` したハンドルIDは再利用されない。閉じ済みハンドルへの
  再アクセス（再 `close` や `call_*`）は `error` を返す。

#### 10.3.1 メモリ確保・文字列・バイト列（Phase3.1）

Phase3のFFIだけでは、(1) 文字列を返すC関数の中身を読めない、(2) 出力引数
（ダブルポインタ）を取るC関数にNULLしか渡せずクラッシュする、(3) 長さ付きの
任意バイト列を安全に受け渡せない、といった制約があった。Phase3.1では以下を
追加してこれらに対応する。

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `myon.ffi.alloc`       | `myon.ffi.alloc(size: int) ret int, error`               | `size` バイトのゼロ初期化済みメモリを確保し、非負のブロックID（`int`）を返す。`size` が0以下なら `error` |
| `myon.ffi.free`        | `myon.ffi.free(block_id: int) ret bool, error`           | ブロックを解放する（成功時 `true`）。無効／解放済みIDは `error` |
| `myon.ffi.read_cstr`   | `myon.ffi.read_cstr(addr: int, max_len: int) ret str, error` | 生アドレス `addr` が指すNUL終端C文字列を `str` として読み出す |
| `myon.ffi.write_bytes` | `myon.ffi.write_bytes(block_id: int, offset: int, data: str) ret bool, error` | ブロック内の `offset` から `data` のバイト列を書き込む（成功時 `true`）。範囲外は `error` |
| `myon.ffi.read_bytes`  | `myon.ffi.read_bytes(block_id: int, offset: int, len: int) ret str, error`    | ブロックの `[offset, offset+len)` を `str` として読み出す。範囲外は `error` |
| `myon.ffi.read_i64`    | `myon.ffi.read_i64(block_id: int, offset: int) ret int, error`                | ブロックの `offset` から8バイトをリトルエンディアンの `int` として読み出す。範囲外は `error` |

**ブロックIDと生アドレスは別の名前空間**

`myon.ffi.alloc` が返す「ブロックID」は、MyonのFFI層が内部で管理する番号で
あり、Cから見た実際のメモリアドレスとは異なる（`myon.ffi.load` のハンドルIDと
同じ考え方）。Myonスクリプト側に生ポインタは一切公開されない。C関数の引数として
メモリのアドレスを渡したい場面では、sig文字に `'b'` を使うことで、FFI層が
呼び出し直前にブロックID→実アドレスへ変換する。

一方、`call_p` の戻り値や `read_cstr` の第1引数 `addr` は「Cの生アドレス値」で
あり、ブロックIDではない。

**`myon.ffi.read_cstr` の詳細**

- `max_len` で読み取る最大バイト数を制限する（暴走読み取り防止）。上限は
  16MB（16777216）で、これを超える値や0以下の値を指定すると `error` を返す。
- `addr` が `0`（NULL相当）の場合は `error` を返す（クラッシュしない）。
- ただし `0` 以外の**不正な**アドレスを渡した場合はクラッシュしうる。
  本フェーズでは signal handler 等による保護は行わない（呼び出し側が
  `call_*` から得た正しいアドレスを渡す責任を持つ）。

```myon
module myon.ffi

handle, err = myon.ffi.load(str("libz.so.1"))

# const char *zlibVersion(void) -> call_p で生アドレスを得る
verptr, e1 = myon.ffi.call_p(handle, str("zlibVersion"), str(""))

# 生アドレスを Myon の str にデリファレンス（最大64バイト）
version_str, e2 = myon.ffi.read_cstr(verptr, 64)
myon.print(version_str)                 # 例: "1.3.1"

ok, clerr = myon.ffi.close(handle)
```

**`myon.ffi.write_bytes` / `myon.ffi.read_bytes` の詳細**

`myon.ffi.alloc` で確保したブロックは「バイト列」の実体として扱える。バイト列は
「ブロックID + 有効データ長」という組で表現し、有効データ長は呼び出し側が別途
`int` で管理する。`write_bytes` / `read_bytes` は、ブロックIDを介してこの領域の
任意のオフセットに読み書きする。範囲外アクセス（`offset+len` がブロックサイズを
超える）になる場合は `error` を返す（メモリ破壊を防ぐガード）。

> **既知の制約（str型のバイナリ安全性）**
> MyonのstrはNUL終端の `char*` として表現され、明示的な長さフィールドを持たない。
> そのため `write_bytes` は `data` の `strlen()` ぶんのバイトを書き込み、途中に
> NULバイト（`0x00`）が含まれると、そこで途切れる。`read_bytes` の結果も同様に、
> 読み出したバイト列にNULが含まれると、可視の文字列がそこで途切れる（ベスト
> エフォート）。任意のバイナリ（NULを含むデータ）を厳密に扱う用途には、将来の
> フェーズで専用のバイト列型を追加する余地を残している。

```myon
module myon.ffi

handle, err = myon.ffi.load(str("libz.so.1"))

# 16バイトのブロックを確保し、"hello"（5バイト）を書き込む
buf, aerr = myon.ffi.alloc(16)
w, we = myon.ffi.write_bytes(buf, 0, str("hello"))

# uLong crc32(uLong crc, const Bytef *buf, uInt len)
# sig "ibi": crc(int) + バッファ('b') + len(int)
crc, ce = myon.ffi.call_i(handle, str("crc32"), str("ibi"), 0, buf, 5)
myon.print(crc)                         # 907060870 (== zlib.crc32(b"hello"))

freed, ferr = myon.ffi.free(buf)
ok, clerr = myon.ffi.close(handle)
```

**出力引数（ダブルポインタ）の呼び出し例**

`myon.ffi.alloc` で確保したブロックを sig `'b'` で渡すと、C関数がそのブロックに
値を書き込む「出力引数」パターンを安全に扱える。書き込まれたポインタ値（8バイト）
は `myon.ffi.read_i64` でリトルエンディアンの `int`（生アドレス値）として読み出し、
続くC呼び出しに `'p'` で渡せる（x86-64のリトルエンディアンを前提とする）。

```myon
module myon.ffi

handle, err = myon.ffi.load(str("libsqlite3.so.0"))
# sqlite3 *db を受け取る8バイト領域（x86-64のポインタは8バイト）
outblock, aerr = myon.ffi.alloc(8)

# int sqlite3_open(const char *filename, sqlite3 **ppDb)
# sig "sb": 文字列引数 + バッファ（ブロックID）引数
rc, oerr = myon.ffi.call_i(handle, str("sqlite3_open"),
                           str("sb"), str(":memory:"), outblock)
# rc == 0 (SQLITE_OK) を期待。

# 書き込まれた db ハンドル（sqlite3*）を生アドレス値として取り出す
db_addr, re = myon.ffi.read_i64(outblock, 0)

# int sqlite3_close(sqlite3 *db) — db_addr は生アドレスなので 'p' で渡す
crc, cerr = myon.ffi.call_i(handle, str("sqlite3_close"), str("p"), db_addr)

freed, ferr = myon.ffi.free(outblock)
ok, clerr = myon.ffi.close(handle)
```

**Phase3.1の既知の制約（まとめ）**

- `'b'` 以外の生アドレス（`'p'` や `call_p` の戻り値）を `read_cstr` /
  `read_bytes` / `read_i64` の対象に使うこと自体は許容するが、`read_cstr` は
  生アドレスを直接デリファレンスするため、不正なアドレスを渡すとクラッシュ
  しうる（本フェーズでは signal handler 等による保護は行わない）。一方
  `read_bytes` / `read_i64` はブロックID経由でのみ動作し、範囲チェックを
  行うため安全である。
- str型はバイナリセーフではない（上記「str型のバイナリ安全性」を参照）。
- 出力引数を持つC関数（例: `sqlite3_open`）は、`'b'` で確保済みブロックの
  アドレスを渡すことで安全に呼び出せる。書き込まれたポインタ値は
  `read_i64` で取り出す。

（`examples/ffi_zlib_version.myon`、`examples/ffi_sqlite_open.myon` に
動作するサンプルがある。）

---

## 11. モジュールシステム

```myon
system myon.useversion=1     // 使用するMyonバージョンの宣言
module myon.stdio               // 組み込みモジュール
module external.util.math as m  // 外部モジュール（./util/math.myon）をmとして読み込み
```

ドット区切りはファイルパス階層に対応する（`.myon` 拡張子を想定）。

---

## 12. エントリーポイント

専用の `main` は不要。トップレベルの文が上から順に実行される。

```myon
system myon.useversion=1
module myon.stdio

x = str("人間")
myon.print("Hello Worlddd! ", x + "!")
```

### 12.1 対話モード（REPL）

`myon` を引数なしで起動すると対話式実行（REPL）モードに入る。

- プロンプトは `myon> `。入力が構文として未完（`()`/`[]`/`{}` が閉じていない等）の
  場合は継続プロンプト `...> ` を表示し、閉じるまで入力を受け付ける
- REPL 内で定義した変数・関数・構造体はセッション終了まで保持される
  （インタプリタと環境を使い回す）
- 実行時エラーが起きても REPL は終了せず、次の入力を受け付け続ける
  （エラーはそのステートメントの中断のみ）
- `exit` / `quit` または EOF（Ctrl+D）で終了する

```
$ myon
myon> x = 1
myon> myon.print(x)
1
myon> exit
```

ファイル実行モード（`myon file.myon`）は従来通り。

---

## 13. EBNF文法定義

```ebnf
program        = { top_level_stmt } ;

top_level_stmt = system_decl | module_decl | statement ;

system_decl    = "system", "myon.useversion", "=", int_literal ;
module_decl    = "module", module_path, [ "as", identifier ] ;
module_path    = identifier, { ".", identifier } ;

statement      = assignment
               | expr_stmt
               | if_stmt
               | while_stmt
               | for_stmt
               | func_decl
               | struct_decl
               | break_stmt
               | continue_stmt
               | return_stmt
               | expose_stmt
               | block ;

statement_list = statement, { ( ";" | newline ), statement } ;

block          = "{", statement_list, "}" ;

assignment     = identifier, [ ":", type ], "=", expression
               | identifier, compound_assign_op, expression ;
compound_assign_op = "+=" | "-=" | "*=" | "/=" ;

expr_stmt      = expression ;

if_stmt        = "myon.if", expression, block,
                 { "myon.elif", expression, "then", block },
                 [ "myon.else", block ] ;

while_stmt     = "myon.while", expression, block ;

for_stmt       = "myon.for", identifier, "myon.in", ( range_expr | expression ), block ;
range_expr     = "range", "(", expression, ",", expression, ")" ;

func_decl      = [ "myon.async" ], "myon.func", identifier, [ type_params ],
                 "(", [ param_list ], ")", "ret", ret_type_list, block ;
type_params    = "<", identifier, { ",", identifier }, ">" ;
param_list     = param, { ",", param } ;
param          = identifier, ":", type ;
ret_type_list  = type, { ",", type } ;

struct_decl    = "myon.struct", identifier, [ type_params ],
                 [ "myon.extends", identifier ],
                 "{", { field_decl | func_decl }, "}" ;
field_decl     = identifier, ":", type ;

break_stmt     = "myon.break" ;
continue_stmt  = "myon.continue" ;
return_stmt    = "ret", expression, { ",", expression } ;
expose_stmt    = "myon.expose", identifier ;

expression     = or_expr ;
or_expr        = and_expr, { "myon.or", and_expr } ;
and_expr       = not_expr, { "myon.and", not_expr } ;
not_expr       = [ "myon.not" ], comparison ;
comparison     = additive, [ comp_op, additive ] ;
comp_op        = "==" | "!=" | "<" | ">" | "<=" | ">=" ;
additive       = multiplicative, { ( "+" | "-" ), multiplicative } ;
multiplicative = unary, { ( "*" | "/" ), unary } ;
unary          = [ "-" ], postfix ;
postfix        = primary, { ".", identifier, [ "(", [ arg_list ], ")" ]
                           | "[", expression, "]"
                           | "(", [ arg_list ], ")" } ;
primary        = literal
               | identifier, [ "<", type, { ",", type }, ">" ]  (* ジェネリック実体化 *)
               | "myon.await", expression
               | lambda_expr
               | "(", expression, ")" ;

lambda_expr    = "myon.lambda", "(", [ param_list ], ")", "ret", type, block ;

arg_list       = arg, { ",", arg } ;
arg            = expression | identifier, "=", expression ;

literal        = int_literal | float_literal | bool_literal
               | str_constructor | char_constructor | error_constructor | string_interp ;

str_constructor   = "str", "(", expression, ")" ;
char_constructor  = "char", "(", expression, ")" ;
error_constructor = "error", "(", expression, ")" ;
string_interp     = '"', { text_fragment | "{", expression, "}" }, '"' ;

type           = "int" | "float" | "str" | "char" | "bool" | "void"
               | "error" | identifier, [ "<", type, { ",", type }, ">" ]  (* struct名／ジェネリック実体化 *)
               | "myon.array", "(", type, ")"
               | "myon.map", "(", type, ",", type, ")" ;

int_literal    = dec_literal | hex_literal | oct_literal ;
dec_literal    = digit, { digit } ;
hex_literal    = "0x", hex_digit, { hex_digit } ;
oct_literal    = "0o", oct_digit, { oct_digit } ;
hex_digit      = digit | "a".."f" | "A".."F" ;
oct_digit      = "0".."7" ;
float_literal  = digit, { digit }, ".", digit, { digit }, [ exponent ] ;
exponent       = ( "e" | "E" ), [ "+" | "-" ], digit, { digit } ;
bool_literal   = "true" | "false" ;

identifier     = letter, { letter | digit | "_" } ;
letter         = "a".."z" | "A".."Z" | "_" ;
digit          = "0".."9" ;
```

---

## 14. 拡張仕様（v0.2確定分）

### 14.1 数値リテラルの拡張

10進数に加えて16進数・8進数・指数表記をサポートする。

```myon
h = 0x1F        // 16進数
o = 0o17         // 8進数
e = 1.5e10       // 指数表記
```

### 14.2 辞書/マップ型

`myon.map(K, V)` を導入する。`myon.array` と対になるコレクション型。

```myon
m = myon.map(str, int)
m.set(str("age"), 20)
v = m.get(str("age"))
m.delete(str("age"))
has = m.has(str("age"))
```

### 14.3 関数内の制御フロー

早期 `ret` のみをサポートする。goto等の任意ジャンプは導入しない
（明示的で読みやすい構文というMyonの設計思想に合わせるため）。

### 14.4 標準ライブラリの初期ラインナップ

`myon.stdio` に加え、以下を最初から用意する。

- `myon.stdio`：`myon.print`, `myon.input`, および以下のファイルI/O関数
  - `myon.file.read(path: str) ret str, error`
  - `myon.file.write(path: str, content: str) ret bool, error`
  - `myon.file.append(path: str, content: str) ret bool, error`
  - `myon.file.exists(path: str) ret bool`
- `myon.math`：数学関数（詳細は別途API仕様で定義）
- `myon.string`：文字列操作関数（詳細は別途API仕様で定義）

ファイルI/Oの詳細な挙動（エラー伝播など）は10.2節を参照。
その他の具体的な関数シグネチャは今後のAPI仕様書で別途定める。

### 14.5 モジュール循環参照

循環参照は**エラー**とする。モジュール解決時に循環を検出した場合、
コンパイル/実行時エラーとして報告する。

### 14.6 struct継承時のフィールド名衝突

子structが親structと同名のフィールドを持つことは**エラー**とする。
オーバーライドは許可しない（型の厳密性を保つため）。

### 14.7 型推論の範囲

関数の引数・戻り値・struct/フィールドの型注釈は引き続き**必須**。
ただし、リテラル直接代入（右辺が単一リテラルの場合）に限り型注釈を省略できる。

```myon
x = 1          // int と推論（省略可）
y: int = 1     // 明示も引き続き可能

myon.func add(a: int, b: int) ret int { ret a + b }  // 関数は型注釈必須のまま
```

### 14.8 ジェネリクス（型パラメータ）

structおよび関数は型パラメータをサポートする。

```myon
myon.struct Box<T> {
    value: T
}

b = Box<int>(value=5)

myon.func first<T>(xs: myon.array(T)) ret T {
    ret xs[0]
}
```

**型制約は導入しない（確定事項）。** `T: Comparable` のような境界指定は
サポートせず、ジェネリクスは型パラメータへの型の代入のみを行う。任意の型を
渡すことができる。現状のジェネリクス実装が「型の代入のみ」というシンプルな
モデルであることを踏まえ、言語の複雑度を抑える実用性重視の判断としてこの方針を
確定する。将来、型制約がないと困る具体的な利用シーンが現れた段階で改めて検討する。

### 14.9 並行処理・非同期処理

`myon.async` / `myon.await` を導入する。

```myon
myon.async myon.func fetchData() ret str {
    ret str("結果")
}

result = myon.await fetchData()
```

**実行モデルは疑似非同期（シングルスレッド）に確定する。**
`myon.async` / `myon.await` はシングルスレッド上での糖衣構文であり、
`myon.async` 関数は呼び出された時点で即座に同期実行される。複数の
`myon.async` 関数が物理的に並行して進行することはない。

この判断は実用性重視の方針による。本格的な並行実行（スレッド／イベントループ）
を導入すると、値の参照カウント（`refcount`）をスレッドセーフにする必要があり、
処理系本体アーキテクチャの大規模な見直しが必要になる。まずは軽量な疑似非同期を
正式仕様として確定し、本格的な並行実行が必要になった場合は別途大規模な設計
フェーズとして切り出す。

---

## 15. Open Questions（残存する未決定事項）

- `myon.map` のキー型に許される範囲（str/int以外の任意型を許すか）
- 標準ライブラリ `myon.math`/`myon.string` の具体的な関数シグネチャ一覧
- 型推論をリテラル以外の式（関数呼び出し結果等）にも広げるか

> 以下は Phase 2 で確定済みのため本リストから除外した。
> - `myon.async`/`myon.await` の実行モデル → 疑似非同期に確定（14.9節）
> - ジェネリクスの型制約 → 導入しないことに確定（14.8節）

---

## 16. 総合サンプルコード

```myon
system myon.useversion=1
module myon.stdio

myon.struct Person {
    name: str
    age: int

    myon.func greet() ret void {
        myon.print("Hello, my name is {self.name}")
    }
}

myon.func divide(a: int, b: int) ret int, error {
    myon.if b == 0 then {
        ret 0, error("ゼロ除算")
    }
    ret a / b, myon.nil
}

x = str("人間")
a = char("a")
b = 1

myon.print("Hello Worlddd! ", x + "!")

myon.if a > b {
    myon.print("aがbよりデカい")
} myon.elif b > a then {
    myon.print("bがaよりデカい")
} myon.else {
    myon.print("しらね")
}

p = Person(name=str("太郎"), age=20)
p.greet()

result, err = divide(10, 0)
myon.if err != myon.nil then {
    myon.print("エラー: {err}")
} myon.else {
    myon.print("結果: {result}")
}

xs = myon.array(int)
xs.push(1)
xs.push(2)
myon.for item myon.in xs {
    myon.print("{item}")
}
```
