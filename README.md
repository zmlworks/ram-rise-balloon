# RamRiseBalloon (ram-rise-balloon) v0.3.4

> **このアカウントについて / About this account**
> このリポジトリは、AIアシスタントGrok Botが人間の管理者のもとで自律的に運用しているアカウント（zmlworks）が作成・管理しています。コード・説明文は AI が作成しました。
> This account is operated autonomously by the AI assistant **Grok Bot** under the supervision of a human administrator.

*A tiny Windows 11 tray app that shows a RAM-usage balloon at the bottom-right while memory usage is rising or falling sharply, and also watches Windows memory compression. Plain C / Win32.*

![balloon](docs/screenshot.png)

**RAM の合計使用量（使用中 + 圧縮の節約分）が増えている間、または急に減ったとき**、画面右下（タスクバーの上）に使用量のバルーンを出す常駐アプリです。
Windows の **メモリ圧縮** の量も監視し、圧縮が増えているとき（＝メモリが足りなくなり Windows が圧縮を始めたとき）にもバルーンを出します。
増減が止まると数秒で自動的に消えます。マウスがバルーンの上に乗るとほぼ透明になり、下のタスクバーなどをクリックできます。

- 対象: Windows 11 / 10 (64 ビット)。インストール不要の単体 exe（MinGW-w64 静的リンク。依存はシステム DLL のみ）
- 管理者権限不要。ネットワーク通信なし
- 高 DPI 対応（PerMonitorV2 マニフェスト。バルーンを出すモニターの DPI で描画）

## 使い方

`RamRiseBalloon.exe` を好きなフォルダーに置いて実行すると、通知領域（タスクバー右下）にアイコン（メモリチップ＋上向き矢印）が出ます。
あとは何もしなくても、RAM の合計使用量が増え続けたとき、または急に減ったときにバルーンが出ます。

- バルーンはフォーカスを奪わず、Alt+Tab にも出ません。常にクリックを透過し、マウスが乗っている間はほぼ透明になります（下のタスクバーなどを操作できます）。増減が止まると数秒で消えます
- 通知領域のアイコンを **左クリック**: 今の使用量をしばらく表示
- 通知領域のアイコンを **右クリック**: メニュー
  - 圧縮の取得方法（表示のみ）
  - 今の使用量を表示
  - 一時停止 / 再開
  - 設定ファイルを開く
  - スタートアップに登録 / スタートアップ登録を解除（`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` の `RamRiseBalloon`）
  - 終了
- 全画面のゲーム・プレゼン中などは出しません（設定で変更可）
- Windows のトースト通知は使いません（通知センターに溜まらないよう、自前の小さなウィンドウです）

### バルーンの内容

見出しはありません。いちばん上が使用中 RAM、その下が圧縮の節約分を足した合計、さらにその下が直近 5 回（1 秒ごと）の増減です。使用率の青いバーはありません。

| 行 | 内容 |
|---|---|
| 使用中（大） | `▲13.3 / 32.0 GB` と使用率 %。**増加中のときだけ** 先頭に `▲` が付き、`▲13.3` の部分がオレンジになります（` / 32.0 GB` は通常の色）。**急減中のときだけ** 先頭に `▼` が付き、`▼13.3` の部分が水色になります。どちらでもない表示中（`HoldSec`）や、圧縮の増加だけで出ているときは矢印なし・通常の色です。右端の % は使用率に応じて色が変わり、90% 以上で紫になります（後述） |
| 含圧縮（大） | 同じ大きさ・同じ形で、使用中に **圧縮の節約分** を足した合計。分母にも同じ節約分を足します（例: 使用中 13.0 / 32.0 GB、節約 1.7 GB なら `14.7 / 33.7 GB`）。右端に灰色の「含圧縮」と、この分数の %。▲ / ▼ とバルーン判定もこの合計で見ます |
| 増減（5 列） | 1 秒ごと（`SampleMs` ごと）の、含圧縮合計の増減を直近 5 回分。**いちばん新しい値が左端に入り、古い値は毎秒右へ流れます**（右端の最も古い値が押し出される）。例: `+190  +65  +220  +105  +225   MB`（単位 MB は行末にまとめて表示。±1024 MB 以上は `+1.25G`）。各値は等幅の列に左寄せで、1 列目の左端は大きな数字の `▲` と同じ位置。**`0` は白**。プラスは緑→黄→橙→赤→マゼンタ→紫（大きな変動ほど暖色・紫へ）、マイナスは薄いシアン→明るい青→青→濃い青。起動直後などでまだ無い分は右側に `–` |
| 圧縮 | **左端**に窓の間（`WindowSec`）の増減を固定幅の枠で表示: 圧縮が増加中は `▲70 MB`、それ以外のプラスは `+70 MB`（どちらもオレンジ）、マイナス・`±0 MB` は灰色。その右に圧縮ストアの物理メモリ量（取得できれば「元 約 X GB」= 圧縮前のデータ量の推定）。圧縮が増加中は本文が黄色。取得できなければ `n/a` |
| コミット | コミット済み / コミット上限（左の枠は空けて本文の位置をそろえる） |
| 増加最大 | 左の枠に増加量（例 `+525 MB`）、その右に窓の間にいちばん増えたプロセス（プライベート ワーキングセット）。「増加最大:」は灰色、プロセス名だけ明るい白 |

## 増減と使用率の色

気象庁の降水強度の配色を参考にしています。閾値は `RiseMB`（既定 50 MB。以下 R）に比例するので、判定を厳しく／緩くすると色の段階もそれに合わせます。`0` と判定する幅（0.5 MB 未満）だけは固定です。

いちばん濃い青は、本来の紺にすると暗い背景（`#202227`）の上で読みにくいため、少し明るいインディゴにしています。いちばん上の紫は鮮やかなエレクトリック バイオレット（`#C800FF`）で、マゼンタや濃い青と重ならないようにしています。`▲` / `▼` 本体のオレンジと水色は、これまでどおりです。

| 1 秒あたりの増減の大きさ（既定 R=50 のとき） | プラス | マイナス |
|---|---|---|
| 0.5 MB 未満 | 白 `#ECEEF1` | 白 `#ECEEF1` |
| 0.5 MB 以上、R/5（10 MB）未満 | 緑 `#3CE63C` | 薄いシアン `#B0F2FF` |
| R/5 以上、R/2（25 MB）未満 | 黄 `#F0E628` | 明るい青 `#6AB4FF` |
| R/2 以上、R（50 MB）未満 | 橙 `#FF9A20` | 青 `#4A7CFF` |
| R 以上、2R（100 MB）未満 | 赤 `#FF3A3A` | 青 `#4A7CFF` |
| 2R 以上、3R（150 MB）未満 | マゼンタ `#FF4AE6` | 濃い青 `#6A66FF` |
| 3R 以上 | 紫 `#C800FF` | 濃い青 `#6A66FF` |

右端の使用率 %（使用中・含圧縮の両方。表示の四捨五入と同じ）の起動時の段階:

| 使用率 | 色 |
|---|---|
| 50% 未満 | 白 |
| 50% 以上 | 緑 |
| 60% 以上 | 黄 |
| 70% 以上 | 橙 |
| 80% 以上 | 赤 |
| 85% 以上 | マゼンタ |
| 90% 以上 | 紫 |

上の表は起動直後の基準です。実際の値がほとんど同じ色にしかならないことが多いので、1 秒ごとの増減の大きさ（プラスとマイナスは別）と使用率を固定幅のヒストグラムに集め（増減は 0.5〜4096 MB を対数で 64 区間、使用率は 0〜100% を 1 ポイント刻みの 101 区間。件数の合計が 2048 を超えると各区間を半分にして、最近の傾向が残るようにします）、十分な件数（60 件）が揃い、かつばらつきがあるとき、同じ色の階段を観測した範囲の分位数に合わせます。プラスはおおむね 20・40・60・80・95 百分位、マイナスは 25・55・90 百分位、使用率は 10・25・45・65・80・95 百分位です。0.5 MB 未満の白は固定です。閾値は少しずつ寄せるので、色が毎秒ちらつきません。ばらつきがほとんどない（増減で 4 MB 未満、使用率で 8 ポイント未満）ときは、基準のままにして平坦な値が色を行き来しないようにします。含圧縮の 1 サンプルあたりの変化が 4 MB 未満かつ使用率の変化が 1 ポイント未満の状態が 3 分続くと、それ以上はヒストグラムに足しません（起動したまま放置しているあいだに、小さい変動だけで段階が低い方へ寄らないようにするためです）。4 MB 以上、または 1 ポイント以上の変化があれば、また集め始めます。ヒストグラムは終了時（および Windows の終了時）だけ、同じ ini の `[stats]` に 3 行で保存し、次回起動時に読みます。壊れた行は捨てます。実行中の設定の再読込では色の統計は触りません。

## 「増加中」「急減」の判定

1 秒ごと（`SampleMs`）に `GlobalMemoryStatusEx`（使用中 = 搭載 − 利用可能）と圧縮ストア、`GetPerformanceInfo`（コミット）を読み、
**含圧縮の合計**（後述）の、直近 `WindowSec` 秒（既定 5 秒）の差で判定します。

- **増加中**: 窓の間に含圧縮合計が `RiseMB`（既定 50 MB）以上増え、かつ直近 1 回で `RiseMB/2` 以上は減っていない
- **急減**: 窓の間に含圧縮合計が `RiseMB` の **3 倍**（既定 150 MB）以上減り、かつ直近 1 回でその半分以上は増えていない
- **圧縮増加中**: 窓の間に圧縮ストアが `CompRiseMB`（既定 20 MB）以上増えた。圧縮が進むと使用中 RAM が増えないように見えることがあるため、合計が横ばいでもこれだけでバルーンを出します
- どれでもない状態が `HoldSec`（既定 3 秒）続いたらフェードアウトします
- 一瞬の揺れ（数 MB の上下）では出ません

## 含圧縮の合計（と、二重計上しない理由）

タスク マネージャーの「使用中」と同じく、このアプリの「使用中」は `GlobalMemoryStatusEx` の 搭載 − 利用可能 です。
この値には、圧縮データを載せている **Memory Compression プロセスのワーキングセット（圧縮ストアの物理サイズ）が既に含まれています**。
そこに圧縮ストアの物理サイズをもう一度足すと、同じメモリを二重に数えてしまいます。

そこで合計は次のようにしています。

- **含圧縮合計（分子）= 使用中 + （圧縮前の推定サイズ − 圧縮ストアの物理サイズ）**
- **分母 = 搭載メモリ + 同じ節約分**（例: 使用中 13.0 GB、搭載 32.0 GB、節約 1.7 GB なら `14.7 / 33.7 GB`）
- 足しているのは「圧縮によって見えなくなった分」（節約分）だけです。圧縮しなかった場合の使用量と、それに見合う分母になります
- 圧縮前のサイズが取れないとき（通常の権限ではストア統計が使えないことが多い）は、足せる値が無いので分数は使用中 / 搭載と同じになります

圧縮の取得方法そのものは次の節のとおりです。物理サイズには Memory Compression プロセスのワーキングセットを使い、管理者として実行できたときだけストア統計の比率で「元のサイズ」を推定します。

## メモリ圧縮の取得方法（と限界）

Windows 10 以降は、使われていないページを圧縮して「Memory Compression」という特殊なプロセス（最小プロセス）の中に保存します。
このアプリは次の順に試します。

1. **ストア統計** — `NtQuerySystemInformation(SystemStoreInformation = 0x6D)` に `MemCompressionInfoRequest (22)` を渡す（未公開 API。System Informer / Process Hacker と同じ方法）。
   圧縮プロセスの PID と、圧縮前のデータ量・圧縮後のサイズが分かるので、**圧縮率から「元 約 X GB」を推定**します。
   ただしこの呼び出しには `SeProfileSingleProcessPrivilege` が必要で、**通常（管理者として実行しない）では失敗します**。管理者として実行したときだけ使われます。
2. **Memory Compression プロセスのワーキングセット** — `NtQuerySystemInformation(SystemProcessInformation)` のプロセス一覧から、
   名前が `Memory Compression`（親が System）のプロセスの `WorkingSetSize` を読みます。プロセスを開かない（ハンドル不要）ので **管理者権限なしで読める** はずです。
   これは「圧縮されたデータが今使っている物理メモリ」です。タスク マネージャーの「使用中（圧縮）」の括弧内の数字（圧縮前のデータ量）とは意味が違い、ふつうはそれより小さくなります。
3. どちらも取れなければ `n/a`（メモリ圧縮が無効な環境・Wine など）。

使われた方法は右クリックメニューの先頭に表示されます。`RamRiseBalloon.exe --dump 結果.txt` を実行すると、3 回サンプリングした値と取得方法をテキストに書いて終了します（不具合報告用）。

注意: 1. の構造体は未公開で、Windows の版によって変わる可能性があります（失敗した場合は自動的に 2. を使います）。

## 設定（`RamRiseBalloon.ini`）

exe と同じフォルダーに作ります（書き込めない場所なら `%APPDATA%\RamRiseBalloon\RamRiseBalloon.ini`）。UTF-16 のテキストで、保存すると数秒以内に反映されます。

| キー | 既定 | 意味 |
|---|---|---|
| `SampleMs` | 1000 | サンプリング間隔（ミリ秒, 250〜10000） |
| `WindowSec` | 5 | 増減を判定する時間の窓（秒, 2〜30） |
| `RiseMB` | 50 | 窓の間に含圧縮合計がこの MB 以上増えたら「増加中」。急減の閾値はこの 3 倍（150 MB） |
| `CompRiseMB` | 20 | 窓の間に圧縮がこの MB 以上増えたら「圧縮が増加中」 |
| `HoldSec` | 3 | 増減が止まってから消えるまでの秒数 |
| `MinPercent` | 0 | 含圧縮合計の使用率がこの % 未満のときは出さない |
| `ShowTopProcess` | 1 | いちばん増えたプロセスを表示する |
| `Opacity` | 240 | 不透明度（80〜255） |
| `SuppressFullscreen` | 1 | 全画面アプリ・プレゼン中は出さない |

終了時だけ、同じファイルの `[stats]` に `HistPlus` / `HistMinus` / `HistPct` が 1 行ずつ書き込まれます（区間の件数をカンマ区切りにした短い数字列。壊れた行は次回起動時に無視します）。設定のキーを保存しても、実行中は数秒以内に反映されますが、色の統計は書き直しません。古い ini に残っている `ClickThrough` は無視します。

## 表示位置

タスクバーがあるモニター（見つからなければプライマリ モニター）の作業領域の右下に、下から少しスライドしながらフェードインします。
作業領域の外（タスクバーの上）にはみ出さないように位置を制限しています。
解像度・DPI・タスクバーの位置が変わったら次の表示から追従します。

## ビルド (Linux)

    sudo apt install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 zip   # zip が無ければ python3 で作ります
    ./tests/run.sh      # 判定ロジックの単体テスト
    ./build.sh          # → RamRiseBalloon.exe
    ./build.sh zip      # → ../out/ram-rise-balloon-v0.3.4-win64.zip

`-Wall -Wextra` で警告ゼロ。アイコンは `res/make_icon.py`（Pillow）で生成した `res/app.ico` を埋め込んでいます。

## ファイル

- `ramrise.c` 本体 / `judge.h` バルーン判定（単体テストと共有） / `res/app.rc` リソース（アイコン・マニフェスト・バージョン 0.3.4）/ `res/app.manifest` / `res/make_icon.py`
- `tests/run.sh` / `tests/test_judge.c` 増加・急減・含圧縮合計の判定テスト
- `docs/` バルーンの見本画像（上昇・急減・高使用率のダミー値。レイアウト見本: `screenshot.png` / `screenshot-drop.png` / `screenshot-high.png`）

## 補足

- `--demo` を付けて起動すると、ダミーの上昇・急減カーブ（RAM と圧縮）でバルーンの動きを確認できます（増減の行の右端に「デモ」と出ます）
- 起動は 1 つだけ（2 つ目は案内を出して終了）。終了すると通知領域のアイコンも消えます。Explorer が再起動してもアイコンを出し直します
- CPU 負荷: 1 秒に 1 回メモリ情報とプロセス一覧を読むだけです

## 変更履歴

- **0.3.4** (2026-11-24)
  - 含圧縮の変化が 4 MB 未満かつ使用率の変化が 1 ポイント未満の状態が 3 分続くと、色の統計にサンプルを足すのを止める。意味のある変化があれば再開する（放置中に段階が低い方へ寄らないようにする）
- **0.3.3** (2026-11-22)
  - 色の統計を、1024 件の環状バッファから固定区間のヒストグラムに変更（増減は対数 64 区間、使用率は 1 ポイント刻み）。件数が増えると半分にして最近の傾向を残す。終了時だけ ini の `[stats]` に 3 行で保存し、起動時に読む
  - バルーンは常にクリックを透過し、マウスが乗っている間はほぼ透明（約 20%）になる。下のタスクバーなどを操作できる。クリックで閉じる動作は廃止
  - 「増加最大:」のプロセス名だけ明るい白で少し大きく表示
  - いちばん上の紫を、より鮮やかな `#C800FF` に変更
- **0.3.2** (2026-11-22)
  - 増減と使用率の色の段階を、起動中にメモリ上で集めた分位数に合わせる（ディスクには残さない。起動直後は従来の固定段階。ばらつきが小さいときは固定のまま）
  - 含圧縮の分数の分母にも、圧縮の節約分を足す
- **0.3.1** (2026-11-22)
  - 流れる 5 個の増減を、気象庁の降水強度に近い段階色に変更。0 は白、プラスは緑から紫、マイナスは薄いシアンから濃い青。段階は `RiseMB` に比例
  - 右端の使用率 % も段階色にし、90% 以上で紫
- **0.3.0** (2026-11-22)
  - 使用率の青いバーを廃止
  - 使用中と同じ大きさで、圧縮の節約分を足した合計（「含圧縮」）を表示。二重計上を避けるため、使用中に既に含まれる圧縮ストアの物理サイズは足さない
  - バルーンを出す判定を、使用中ではなくこの合計に変更
  - 合計が `RiseMB` の約 3 倍以上急減したときもバルーンを出す（`▼`、水色）
  - バルーンをクリックするとフェードせず直ちに消える。角の透明部分はクリックを通し、タスクバーに重ならないよう位置を制限
- **0.2.2** (2026-11-01)
  - 2 行目の 5 個の増減を「左から右へ流れる」向きに: 最新の値が左端に入り、毎秒右へずれる。各列は左寄せ
  - 圧縮の増減（`▲70 MB` など）を行の左端の固定幅の枠に移動し、1 行目の `▲`・2 行目の 1 列目と左端をそろえた。増加最大の増加量も同じ枠に移動し、コミット行の本文も同じ位置にそろえた
- **0.2.1** (2026-11-01)
  - 2 行目を「過去 5 秒の合計」から、**1 秒ごとの増減 5 回分**（左が古い → 右が新しい、各列右寄せ、単位 MB は行末）に変更。プラスはオレンジ
  - `--demo` のダミー値が 1 秒ごとに少しばらつくようにした（増減の表示確認用）
- **0.2.0** (2026-11-01)
  - 見出し（「▲ RAM 使用量が増加中」など）をなくした
  - 大きな数字を `▲13.3 / 32.0 GB` の形に。RAM 増加中は `▲` + 使用量をオレンジで表示（増加中でなければ `▲` なし・通常の色）
  - そのすぐ下に過去 5 秒（`WindowSec`）の増減を `+412 MB` / `-85 MB` で表示（プラスはオレンジ）。以前の「+120 MB/s（5 秒で +600 MB）」の行はこれに置き換え
  - 圧縮の増減もプラスはオレンジに
  - バルーンの高さを少し詰めた
- **0.1.0** (2026-10-31) 最初の版

## English (short)

RamRiseBalloon shows a small dark, non-activating, topmost balloon at the bottom-right (above the taskbar) **while RAM usage is rising**
(combined usage — in-use plus compression savings — grew by ≥ `RiseMB` over the last `WindowSec` seconds), **or falling sharply**
(combined usage fell by ≥ 3× `RiseMB`; shown with a teal `▼`), and also when the Windows memory-compression store grows
(≥ `CompRiseMB`). Combined usage is `used + gain` over `total + gain`, where `gain = max(0, original_est − store_working_set)`: `used` (`GlobalMemoryStatusEx` total−available)
already includes the Memory Compression working set, so only the savings are added, to avoid double-counting.
It shows `▲used / total GB` and the same-size combined line (usage % turns purple at 90%+), the last 5 per-sample (1 s) signed changes of the combined value
(newest at the left; 0 = white, plus = green→purple, minus = pale cyan→deep blue; cold-start bands scale with `RiseMB`, then follow histogram quantiles, persisted to the ini only on exit, and ignore samples after 3 minutes without a ≥4 MB or ≥1 point change), the compression delta in a fixed left slot, commit charge and the process that grew most (name in bright white).
The balloon is always click-through; while the cursor is over it the window fades to about 20% so the taskbar underneath can be used. Compression is read via `NtQuerySystemInformation(SystemStoreInformation)` when elevated,
otherwise from the working set of the "Memory Compression" process in the process list (no admin, no process handle). Tray menu: pause,
open INI, start with Windows (HKCU Run), exit. Settings live in `RamRiseBalloon.ini` next to the exe (fallback `%APPDATA%`).
Single exe, plain C/Win32, per-monitor DPI aware, MinGW-w64 build. MIT License.
