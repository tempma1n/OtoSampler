# OtoSampler
![OtoSampler](otosampler.png)
## これはなに
音madを想定してClaudeで作ったサンプラーです  
声の加工を想定として作られているので他の素材だと上手くいかないかもしれません  
 
## 導入
リリースから.vst3をダウンロードしVST3フォルダに入れてください  
macOSは.vst3を ~/Library/Audio/Plug-Ins/VST3 に .componentを ~/Library/Audio/Plug-Ins/Components に入れてください  
macOSで読み込めない場合はターミナルで xattr -cr "Oto Sampler.vst3" や codesign --force --deep --sign - "Oto Sampler.vst3" を実行してください  

## 各種機能(大雑把)
Formant Hold フォルマント保持   
Spped Hold スピード保持  
Pitch Flatten ピッチを平均化  
Denoise Pitch Flatten Formant Holdで起きたノイズを除去  
Pre-Filter サンプルが鳴る前にLPHPフィルターを調整  
Glide ポルタメント BendでMIDIピッチベンドの深さ変更  
Sustain loop 母音等の声を綺麗に伸ばすループ(素材に依存)  
Normalize 音割れしないギリギリまで音量を上げる  
Auto Root Rootkeyを自動で設定(Pitch Flattenをオンにて推奨)  
OSC addで3基まで追加可能 右クリックでコンテキストメニューを表示
## ショートカット
ノブ上でダブルクリックで数値を初期化  
波形上で左クリック サンプルを再生  
波形上で左クリック+Ctrl(macOSはCmd) 狭いループをクリック箇所に追加(ループ有効)  
ピッチノブでShiftを押しながら回すとオクターブ単位でピッチの数値を変更  
Ctrl+C OSCの数値を全てコピー  
Ctrl+V OSCの数値を違うOSCに全て貼り付け  
Ctrl+D OSCを複製
Del OSCを削除

## ライセンス

[GNU Affero General Public License v3.0](LICENSE).  
[Claude](https://claude.ai/) — AI-assisted development.  
