# Unity Wi-Fi / USB controller protocol（KP / KD / KJ / KA v1）

`unity_controller` は BNO085 とボタンの最新状態を10ms周期（100Hz目標）で、Wi-Fi UDPとUSBへ個体・セッション付きの同じ88バイトフレームとして送ります（内側の姿勢データは64-byte KJ v1）。スマホも10ms周期で定期送信しますが、ブラウザ側のセンサー更新頻度は端末に依存します。ESPとPCを同じLANにつなげれば直接通信でき、受信機ESPは不要です。本番用PlatformIO環境は共通の `unity_controller` です。元からあったセンサー実験用の `gyro_test` / `gyro_test2` は維持します。

Unity側はUDP受信スレッドとシリアル受信スレッドでCRC・順序・時刻・姿勢を検証し、最新サンプルを渡します。UDPは1 datagramを1フレームとして検証し、USBはバイト列からフレーム境界を復元します。Pythonブリッジや外部WebSocketサーバーは不要です。

## フレーム

整数・IEEE-754 float はすべて little endian。CRCに含むのはoffset0〜59。

| Offset | サイズ | 内容 |
|---:|---:|---|
| 0 | 2 | magic ASCII `KJ` (`4b 4a`) |
| 2 | 1 | version `1` |
| 3 | 1 | length `64` |
| 4 | 1 | group `0..2` |
| 5 | 1 | slot `0=P1`, `1=P2` |
| 6 | 1 | flags: bit0 Guard, bit1 OrientationValid, bit2 GyroscopeValid, bit3 AccelerationValid, bit4 RecenterHeld |
| 7 | 1 | reserved `0` |
| 8 | 4 | uint32 sequence。各送信で増加、wrapあり |
| 12 | 4 | uint32 device millis。wrapあり |
| 16 | 2 | uint16 recenterSequence。リセットを0.5秒未満で離したときだけ増加 |
| 18 | 2 | reserved `0` |
| 20 | 16 | quaternion float x/y/z/w |
| 36 | 12 | angular velocity float x/y/z、degrees/second |
| 48 | 12 | linear acceleration float x/y/z、m/s² |
| 60 | 4 | CRC-32/IEEE、poly `edb88320`、init/final XOR `ffffffff` |

BNOのquaternion `(i,j,k,real)` は正規化して送信quat `(real,-i,-k,j)` へ写像し、線形加速度 `(x,y,z)` は `(-y,-z,x)` へ写像します。実機の上振り・右振り・前への突きから補正した固定の取り付け変換です。旧送信quat `(j,k,i,real)` に対し、ワールド座標をY軸まわりに180度、コントローラのローカル座標をZ軸まわりに180度回します。姿勢は `world * oldRotation * inverse(local)`、加速度はローカル変換のみを適用します。ワールド基底と剣のローカル基底を別々に補正するため、同一の軸変換でquatを共役する実装にはしません。変換は `include/controller_axes.h` にまとめています。

quatは右手系、Unityでスマホ入力と同じ反射 `(-x,-y,z,w)`、gyro `(-x,-y,z)`、acceleration `(x,y,-z)` を適用します。スマホ側とUnityの受信時の反射変換は変更しません。Unityは初回・入力源変更・リセンターで基準姿勢を更新します。リモコンのメニューはゲームと同じ固定の基準姿勢からの相対姿勢を使い、上振りを-X、右振りを+Y、ひねりをZとして判定します。BNOの起動時のワールド方位を直接使わず、メニュー遷移や振るたびに基準を取り直すこともありません。スマホのメニューは従来の固定ワールド座標を使います。リセット短押しはメニュー・プレイ中とも「向きを合わせる」です。bit4を連続して0.5秒受信すると、プレビュー中は「戻る」、それ以外ではEscと同じ接続ポーズを開き、長押し解放ではrecenterSequenceを増やしません。スマホ56-byte v1にも同じbit4/短押しカウンタを追加しています。

センサー不在でもidentityのハートビートを送ります。姿勢・加速度flagsは0ですが、GuardとRecenterHeldは送信します。検出後は新しい姿勢・加速度を別々に有効にし、姿勢は150ms、加速度は100msで無効化します。姿勢の更新が1秒以上止まった場合は3秒間隔でARVRレポートを再要求し、10秒以上止まった場合だけセンサーを初期化し直します。一時的な不正サンプルでも最後の有効姿勢の時刻を保持し、短い欠落で再初期化を繰り返しません。端末の接続と有効な姿勢は別です。Unityでも受信が古くなるとガードを解除します。

現在のUnity実装は **選択した1組の2人** を扱い、既定はgroup0です。Unity起動前に `KOSEN_CONTROLLER_GROUP=0..2` を設定すると対象組を変更できます。選択していない組は入力へ反映しません。フレーム形式は今後の3組×2台にも対応しますが、6台を同時に扱う実装・実機検証はしていません。各リモコンのgroup/slotは重複させないでください。

`group` はゲーム台・対戦組の番号、`slot` はその組のP1/P2です。ビルドや書き込みを行ったPCの識別子ではなく、どのPCから書き込んでも対応する組のUnityを操作できます。書き込み元PCへの自動ペアリングは行いません。

2026-10-15にUnity用書き込み環境を `unity_controller` の1種類へ統一しました。group/slotは登録時にゲームから受け取り、ESPのNVSへ保存します。裸のKJパケットをブロードキャストして割り当てる旧運用は使いません。[ゲーム内コントローラ登録仕様](../../../docs/CONTROLLER_PAIRING.md) も参照してください。

## 登録用KP v1（48 bytes）

PCはUDP42042、ESPはUDP42043を使います。整数はlittle endianです。

| Offset | サイズ | 内容 |
| ---: | ---: | --- |
| 0 | 2 | ASCII `KP` |
| 2 | 1 | version 1 |
| 3 | 1 | kind: Request=1 / Offer=2 / Commit=3 / Confirm=4 / Discover=5 / Found=6 |
| 4 | 8 | device ID（ESPのeFuse MAC） |
| 12 | 8 | host ID（PCで生成・保存する非ゼロ64bit値） |
| 20 | 8 | request ID（ESPが要求ごとに生成） |
| 28 | 8 | session ID（PCが登録候補ごとに生成） |
| 36 | 1 | slot 0=P1 / 1=P2 |
| 37 | 1 | group 0..2（Unityの対象group） |
| 38 | 3 | 台のLED色 RGB |
| 41 | 3 | reserved 0 |
| 44 | 4 | 先頭44 bytesのCRC32/IEEE |

ガード＋リセット1秒同時押しでRequestをブロードキャストし、登録待ちのPCがOfferを返します。ESPは最初の候補を受けてから1秒間ほかの候補を確認し、複数host/session/IPなら登録を中止します。候補が1つならCommitをそのPCへ送信し、Confirmを受けてNVSへ保存します。PCは新セッションの正常な最初の姿勢フレームを受けて登録を完了します。Request/Commitは400ms間隔で再試行し、10秒で中止。PC側の候補は12秒で期限切れです。

通常送信先のIPへ応答が1.2秒来なくなったら、保存済みhost/session/deviceを指定するDiscoverを2秒間隔でブロードキャストします。保存した登録に一致するPCだけがFoundを返し、ESPは今回のrequest ID・host/session/slot/groupを照合して返信元IPへ更新します。他PCへ自動で乗り換えません。

## データ用KD v1（入力88 bytes / 応答40 bytes）

| Offset | サイズ | 内容 |
| ---: | ---: | --- |
| 0 | 2 | ASCII `KD` |
| 2 | 1 | version 1 |
| 3 | 1 | フレーム全長（入力88 / 応答40） |
| 4 | 8 | device ID |
| 12 | 8 | session ID |
| 20 | 64 / 16 | KJ v1入力 / KA v1接続・振動応答 |
| 全長-4 | 4 | 直前までの全バイトのCRC32/IEEE |

通常データは登録したPCへユニキャストします。登録前は操作データを送信しません。PCは外側のdevice/sessionと内側のgroup/slot・CRC・連番を照合し、未登録の個体、古いセッション、裸の旧KJを拒否します。UDPは1 datagramで1 envelope、USBは任意のRead境界から88-byte envelopeを復元します。USBで別PCへ接続しても、そのPCに未登録なら入力には使いません。応答もKDで包むので、別セッションの振動・接続応答をESPが採用しません。

PCの保存先は `~/.kosen-chanbara/controller-pairing.json`、ESPの保存先はPreferences名前空間 `kj-pairing` です。PCのファイルはPCごとに生成し、ほかの台へコピーしません。CRCとセッション照合はLAN内の誤配送防止用で、暗号化や相手の認証を提供するものではありません。

## ゲーム接続の応答とLED

Unityは受理したUDPパケットに対し、送信元IP/portへ最大10HzでKA v1の応答をKDで包んだ40 bytesを返します。`KA` magic、version=1、length=16、group、slot、vibrationMs uint16（0=heartbeat、25=ガード、50=直撃）、受理したsequence uint32、先頭12バイトのCRC32 uint32を、すべてlittle endianで格納します。違う組、不正CRC、未受理の連番には応答しません。受信スレッドの停止で応答も止まります。

ESPはgroup/slot/CRCと送信済み連番との差150以内を検証し、最新の応答から1.2秒以内ならGPIO3をHIGH、GPIO2をLOWにします。未接続では逆になります。Wi-Fi接続だけではゲーム接続LEDは点灯しません。姿勢が無効でも正常heartbeatへの応答はゲーム接続として扱い、ゲーム側は別途センサーの無効状態を表示します。ピンは`KJ_CONNECTED_LED_PIN`と`KJ_DISCONNECTED_LED_PIN`で変更できます。

## Wi-Fi設定と共通ファームウェアの書き込み

ルート `.env` の `WIFI_SSID1` / `WIFI_PASS1` から `WIFI_SSID3` / `WIFI_PASS3` のうち、`just controller 1` / `2` / `3` で選んだ1組を使います。番号なしの `WIFI_SSID` / `WIFI_PASS` は1番として扱います。`KJ_WIFI_CONFIG_FILE` で別ファイルを指定することもできます。Wi-Fi設定ヘッダーはビルド時に生成し、コミットしません。

```sh
# リポジトリのルートで実行。番号がWi-Fi設定を選ぶ。
just controller 1
just controller 2
just controller 3
PORT=/dev/cu.usbmodem... just controller 2
# ビルドだけ
python3 scripts/controller_test.py build 2
# PlatformIOを直接使う場合
pio run --project-dir apps/kojo_HW -e unity_controller
pio run --project-dir apps/kojo_HW -e unity_controller -t upload --upload-port <USBポート>
```

ESPは1台ずつUSB接続するか、ポートを明示指定します。UnityをPlay中にしていてもUSB esptoolで書き込みできます。書き込み後はゲーム内からP1/P2へ登録し、USBを外して給電だけでも通信できます。ESP32-C3のネイティブUSB用に `ARDUINO_USB_MODE=1` と `ARDUINO_USB_CDC_ON_BOOT=1` を指定します。本番用のP1/P2別・PC別環境は設けません。元からあったセンサー実験用の `gyro_test` / `gyro_test2` は維持します。Unity用はテスト用・UART用も含め `unity_controller` の1種類だけにします。

元からあったセンサー実験用ターゲットは、従来どおり環境を明示して実行できます。

```sh
pio run --project-dir apps/kojo_HW -e gyro_test
pio run --project-dir apps/kojo_HW -e gyro_test2
```

登録済みPCへのUDP42042番ユニキャストを使います（ESP側42043番）。Unityの受信portは `KOSEN_CONTROLLER_UDP_PORT` で変更できますが、ESP側 `KJ_UDP_PORT` と合わせる必要があります。Wi-Fi省電力は無効で、10秒ごとにWi-Fi再接続を試みます。登録時と登録済み相手の再探索だけブロードキャストします。PCの固定IP、書き込み時のP1/P2・PC指定は不要です。

PCとESPは端末間通信が可能な同一LANにつなぎ、PC側でUDP42042番の受信を許可します。PCは有線接続でも構いません。iPhoneのテザリングを使う場合は「互換性を優先」をオンにして2.4GHz接続を使います。

スマホの起動・停止は `just phone-start` / `just phone-stop` です。スマホは接続中のslotで優先され、切断すると実機へ戻ります。キーボードは併用できます。

## 検証

```sh
c++ -std=c++11 -Wall -Wextra -Werror test/native/protocol_test.cpp -o /tmp/kojo-protocol-test
/tmp/kojo-protocol-test
c++ -std=c++11 -Wall -Wextra -Werror test/native/controller_axes_test.cpp -o /tmp/kojo-controller-axes-test
/tmp/kojo-controller-axes-test
```

CRCの標準check値、パケット破損、修復、group範囲、長押しフラグと未定義flagsを検証します。軸変換のテストは実機の同じ持ち方からの上振り2回・右振り2回を使い、360deg/sを超えたときのメニュー方向、剣のローカル回転方向、quaternionの符号反転、逆方向の動き、姿勢と加速度の座標系の一致、前への突きの加速度を検証します。Unity EditMode testsでUDPのdatagram境界・送信元の切替・CRCと、USBのfragmentation・同期復帰・sequence/millis wrap/reboot・無効poseを検証します。実機の接続・センサー軸・GPIO極性はESPからの受信で確認します。

2026-10-05の初回実機確認（後に更新停止が見つかった旧3レポート版）: OTAで書き込んだP1から、6秒で598件の重複を除いた正常パケットを受信し、すべて姿勢・角速度・線形加速度が有効でした。Unity 6000.3.21f1の再生中にも10秒で960件の有効入力を確認し、入力元 `Hardware`、剣の姿勢が有効であることを確認しました。センサーを静止させた状態での検証で、P2実機と振り・ボタン操作の確認は含みません。

同日の新版（SH-2イベントを個別に処理する姿勢＋加速度版）はP1/P2ともビルド済み。P1は `/dev/cu.usbmodem1101` へのUSB書き込みとフラッシュhash検証まで完了しました。書き込み後の実センサーデータと突き動作は未確認、P2実機への書き込みは未実施です。

参照: [Espressif Wi-Fi](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html)、[Adafruit BNO085 Arduino](https://learn.adafruit.com/adafruit-9-dof-orientation-imu-fusion-breakout-bno085/arduino)。

2026-10-09の実機確認（60Hz変更前）: P1へのUSB書き込みとhash検証に成功。ネイティブUSBで3秒間に301件のCRC正常パケット（約100Hz、姿勢・加速度有効）を受信し、iPhone経由のUDPでも3秒間に285件（約94Hz）を受信しました。送信は10ms周期で、無線の受信件数は損失・ジッタを含みます。Unity 6000.3.21f1のBattleGround再生中にも `UDP 172.20.10.3:42043` からP1入力を受理し、入力元 `Hardware` と有効な剣姿勢・加速度を確認しました。P2はビルド済みで実機への書き込み・操作は未確認です。

無線の切り分けでは、USBポートを使うUnityのPlayを停止して115200 baudのUSBへASCII `?` を送ると、1回だけ `KJ_STATUS` を返します。`wifi=3` と非ゼロの `ip`、`udp=1` がWi-Fi接続済みです。`ssid` は接続先、`rssi` は電波強度、`host` は保存したゲームPCのIP、`paired` は保存済み登録の有無です。`pairState` は0=待機、1=登録要求、2=候補確認、3=確定待ち、`conflict=1` は複数候補による登録中止を示します。`ackAgeMs` は最後のゲーム応答からの経過時間（-1=起動後未受信）で、応答には既存USB経路のものも含みます。`reason` は最後のWi-Fi切断理由で、現在の接続失敗を意味するとは限りません。パスワードは出力しません。通常の88-byte KD入力は継続し、Unityのストリーム復元処理は診断文字列を読み飛ばします。MacのWi-Fiを変えずに、別LANへ接続したESPの状態をUSBから調べられます。

2026-10-09の追加指定により、スマホと物理コントローラの定期送信を60Hzへ変更しました。ESPは16667µs周期を基準にし、スマホは約16.67msの期限から次のタイマーを計算します。整数msへの丸めによる約62.5Hz化や、遅延後の連続送信を避けます。スマホのボタン操作時の即時送信とセンサー自体の更新周期は維持します。Unityの受信形式・割り当て・タイムアウトは変更不要です。

60Hz変更後のP1実機検証: USB書き込み・hash検証に成功。起動直後のバッファを除いてパケット連番と端末時刻から求めた送信レートは約60.01Hzでした。P2は同じ変更でビルド済み、実機書き込みは別途必要です。

2026-10-14の指定で定期送信をスマホ・P1/P2とも10ms周期（100Hz目標）に戻し、物理コントローラの線形加速度レポートも10ms周期へ変更しました。姿勢レポートは5ms周期のままです。遅延後にパケットを連続送信せず、ボタン操作時のスマホ即時送信は維持します。実機への書き込みと100Hzでの受信確認は別途必要です。

2026-10-09の軸補正: 同じ持ち方からの上振り2回・右振り2回・前への突き2回を記録し、リモコン側の姿勢と加速度の座標系を補正しました。スマホ側・Unity側の入力処理と360deg/sのメニュー閾値は変更していません。記録した入力を使うnative軸テストとprotocolテスト、通常・実験用P1/P2の4プロファイルのビルドが成功しました。接続中のP1へUSB書き込み・hash検証まで完了し、書き込み後の10秒の受信で姿勢・加速度有効な600件のCRC正常パケットを確認しました。起動直後のバッファを除いた通常動作中の端末時刻から求めた送信レートは60.00Hzでした。Unity 6000.3.21f1のEditMode入力テスト85件とPlayMode入力切替テスト26件も成功しました。補正後に実際のゲームを操作する確認と、P2実機への書き込みは未実施です。

2026-10-09のメニュー方向補正: リモコンのメニューはBNOの絶対ワールド方位から、ゲームと同じ固定のリセンター基準へ変更しました。起動姿勢6種類で上・下・左・右と両方向のひねり、記録した上振り2回・右振り2回を同じリセット基準で再生するケース、スマホがリモコンの基準姿勢の影響を受けないことを検証しました。UnityのEditMode入力テスト89件、PlayMode入力切替テスト26件、native軸テストが成功しました。360deg/sの閾値、スマホの入力処理、剣の姿勢計算、ファームウェアの送信処理は変更していません。


## 旧PC別書き込み方式からの変更（2026-10-15）

旧方式はPCのIP・P1/P2・LED色をevent用ファームウェアへ埋め込んでいました。この方式と専用環境は廃止しました。`scripts/event_controllers.py setup` は書き込まずに新しい `just controller` を案内します。旧 `.controller-ap/event.json` は新しい実行時登録には使用しません。以下の実機部品についての記録は維持します。

RGBは最新HW実験コードと同じNeoPixelBus・GPIO20、振動は回路図のpwmネット（ESP32-C3 IO7）を使う。振動ACKはWi-FiとUSBで同じ16バイト・CRC32形式とし、異なるslot・古い連番・不正CRCは無視する。USB接続だけでも振動を受け付ける。

センサーはBNO055（0x29→0x28）を検出し、見つからなければ従来BNO08x（0x4B→0x4A）を使う。BNO055は姿勢・線形加速度を10msごとに読む。どちらも送信目標100Hz、姿勢差分からゲーム側が角速度を求める。参考としてremote mainのf135b01を取得し、BNO055とGPIO20のRGB実験を確認した。

2026-10-15の振動調整: IO7のモーターは20kHz PWM、デューティ192/255（約75%）で駆動し、従来の全出力から回転を弱める。ガード50ms・直撃100msの長さとACK形式は維持する。PWM切替は開始/停止時だけ行い、センサー処理のループで再設定し続けない。

2026-10-16: モーター出力を直前の2/3へ変更（PWM 192→128/255）。20kHz、ガード50ms・直撃100msは維持する。PWMは出力設定であり、実回転数のフィードバック制御は行わない。

2026-10-16追加: Unityから送る振動時間をガード25ms・直撃50msへ半減。duration値をそのまま駆動する既存ファームウェアで動作し、PWM 128/255は変更しない。
