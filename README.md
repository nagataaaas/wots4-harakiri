# Harakiri（一撃死）

侍道4 Steam版向けの一撃死モードです。プレイヤーとNPCの攻撃ダメージを非常に大きくし、斬撃では死亡、峰打ちでは活力0とゲーム本来の気絶処理を発生させます。

現在は開発版です。実ゲームでの動作は要ゲーム内確認です。イベント中の特殊な無敵状態や通常のダメージ処理を通らない攻撃は対象外になる場合があります。

ModLoaderとともに `mods/harakiri/mod.dll` と `config.ini` を配置してください。`config.ini` の `[harakiri] enabled=1` で有効、`0` で無効になります。ゲームファイルは書き換えず、起動時に対応する版とコードを照合してメモリ上にフックします。

ビルドはModLoaderリポジトリで `cmake -S . -B build -A Win32`、`cmake --build build --config Release` を実行します。出力は `build/game/mods/harakiri/mod.dll` です。単体ビルドでは `-DWOTS4_SDK_DIR=<ModLoaderのパス>` を指定してください。

テストは `ctest --test-dir build -C Release -R harakiri --output-on-failure`。ゲームを起動しません。
