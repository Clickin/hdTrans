import {
  HdTrans,
  type InitOptions,
  type UserDictEntry,
  type UserDictPos,
  type ChoiceSelection,
  type TranslationChoice,
  type TranslationResult,
  type EhndFiles,
} from 'hdtrans';

async function check() {
  const options: InitOptions = { ehnd: { 'PreFilter_test.txt': '' } };
  const translator: HdTrans = await HdTrans.init(options);
  const plain: string = translator.translate('学生');
  const explained: TranslationResult = translator.translate('学生', { json: true });
  const explicitPlain: string = translator.translate('学生', { json: false });
  const dynamic: string | TranslationResult = translator.translate('学生', { json: Math.random() > 0.5 });
  const pos: UserDictPos = 'v:ㄷ';
  const rows: UserDictEntry[] = [{ ja: '聞く', ko: '듣다', pos }];
  translator.setUserDict(rows);
  const active: UserDictEntry[] = translator.getUserDict();
  translator.setUserDictRaw('学生\t학우\n');
  const raw: string = translator.getUserDictRaw();
  const dictionaryVersion: string = translator.dictionaryVersion;
  const runtime: 'wasm' = translator.runtime;
  const error: string = translator.userDictError;
  const errors: string[] = translator.filterErrors;
  const files: EhndFiles = { 'PostFilter_test.txt': '' };
  translator.setEhnd(files);
  translator.clearEhnd();
  translator.clearUserDict();
  translator.reset();
  translator.close();
  const choice: TranslationChoice | undefined = explained.choices[0];
  const selection: ChoiceSelection | undefined = choice?.choice;
  const logs: string[] = explained.logs;
  void [plain, explicitPlain, dynamic, active, raw, dictionaryVersion, runtime, error, errors, selection, logs];

  // @ts-expect-error Engine implementation switches are not public npm options.
  await HdTrans.init({ reference: true });
  // @ts-expect-error Engine implementation switches are not public npm options.
  await HdTrans.init({ cg: false });
  // @ts-expect-error Engine implementation switches are not public npm options.
  await HdTrans.init({ misread: true });
  // @ts-expect-error The package stages its own module and dictionaries.
  await HdTrans.init({ directories: ['/dicts'] });
  // @ts-expect-error JSON mode returns a structured result.
  const wrong: string = translator.translate('学生', { json: true });
  void wrong;
}
void check;
