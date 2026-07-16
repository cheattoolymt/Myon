# Myon言語仕様書 v0.1

## 0. 概要

**Myon** は実用性を見据えた、型に厳密なプログラミング言語である。
`myon.` プレフィックスによる名前空間の明示、`str()`/`char()`/`int()` のような値コンストラクタ風の型構文、
そして独自のスコープ公開機構 `myon.expose` を特徴とする。

### 0.1 名称の由来（小ネタ）

「Myon」という名称は、東方Projectの二次創作における
魂魄妖夢の「みょん」に由来する。

### 0.2 設計方針まとめ

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

```myon
name = myon.input("お名前は？")   // str型で受け取る
myon.print("Hello Worlddd! ", x + "!")
```

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

- `myon.stdio`：`myon.print`, `myon.input`
- `myon.math`：数学関数（詳細は別途API仕様で定義）
- `myon.string`：文字列操作関数（詳細は別途API仕様で定義）

具体的な関数シグネチャは今後のAPI仕様書で別途定める。

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

### 14.9 並行処理・非同期処理

`myon.async` / `myon.await` を導入する。

```myon
myon.async myon.func fetchData() ret str {
    ret str("結果")
}

result = myon.await fetchData()
```

具体的な実行モデル（スレッドベースか、イベントループベースか等）は
処理系実装フェーズで別途検討する。

---

## 15. Open Questions（残存する未決定事項）

- `myon.async`/`myon.await` の具体的な実行モデル（スレッド/イベントループ）
- ジェネリクスにおける型制約（`T: Comparable` のような境界指定）の要否
- `myon.map` のキー型に許される範囲（str/int以外の任意型を許すか）
- 標準ライブラリ `myon.math`/`myon.string` の具体的な関数シグネチャ一覧
- 型推論をリテラル以外の式（関数呼び出し結果等）にも広げるか

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
