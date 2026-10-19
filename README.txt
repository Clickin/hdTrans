hdTrans

1. 원하는 폴더에 압축을 풀면 바로 쓸 수 있습니다. 설치는 필요 없습니다.

2. 텍스트나 파일은 hdTransGUI.exe 또는 hdtrans.exe로 번역합니다.
   Linux와 macOS에서는 hdtrans를 씁니다.
   예: echo 今日はいい天気ですね。 | hdtrans.exe
   예: hdtrans.exe -in input.txt > output.txt
   버전 확인: hdtrans.exe -version

3. 이지트랜스용 도구(LunaTranslator, XUnity.AutoTranslator, 아랄트랜스,
   VNR, 아네모네)에서는 J2KEngine.dll을 불러오는 위치에 J2KEngine.dll과
   hdTrans 폴더를 함께 둡니다. J2KEngine.dll은 32비트입니다.
   아랄트랜스와 VNR은 레지스트리 HKCU\Software\ChangShin\ezTrans의
   FilePath 값을 읽습니다. 이 값은 DLL이 있는 폴더로 지정합니다.
   도구별 안내: https://github.com/sokcuri/hdTrans/blob/main/docs/using-with-tools.md

4. 사용자 사전과 설정은 hdTrans\user\에 둡니다.
   Linux와 macOS에서는 프로그램 옆 user/에 둡니다.
   userdict.example.txt를 참고해 userdict.txt를 만드세요.
   여러 파일로 나누려면 userdict로 시작하는 .txt 이름을 씁니다.
   설정은 hdtrans.config.example.yaml을 hdtrans.config.yaml로 복사해 고칩니다.
   업데이트는 같은 폴더에 덮어 풀면 됩니다. 사용자 파일은 유지됩니다.
   배포본의 견본 파일은 수정하지 마세요. 업데이트 때 교체됩니다.

5. Ehnd(꿀도르)는 기본으로 꺼져 있습니다. 켜면 번역 품질이 낮아질 수 있습니다.

Windows의 hdtrans.exe와 hdTransGUI.exe는 64비트입니다.
32비트 환경에서는 hdtrans_x86.exe를 씁니다.
hdTransEngine.dll과 hdTransEngine_x86.dll은 다른 프로그램에 넣어 쓰는 개발자용 파일입니다.

소스와 안내: https://github.com/sokcuri/hdTrans
라이선스: AGPL-3.0-or-later, 외부 라이브러리는 THIRD-PARTY-NOTICES.txt를 확인하세요.
