# Hydroid 리소스 시스템 설계 및 개발 로드맵

Nov 14, 2026 · @fmthead

## 개요

리소스 계층은 C++17 공유 라이브러리 `hydroid-res` 하나로 만들고, UIKit·GTK4·WinUI 3 쪽에는 현재 기기 조건을 넘겨주는 얇은 어댑터만 둔다. 같은 `res/` 폴더와 같은 기기 조건이면 세 플랫폼 모두 안드로이드와 똑같은 값을 골라야 한다.

- **대상:** iOS(UIKit, Swift), 리눅스(GTK4, C++), 윈도우(WinUI 3, C++/WinRT)
- **입력:** 안드로이드 소스 `res/` 폴더. 컴파일된 APK(`resources.arsc`, 바이너리 XML) 읽기는 범위 밖이며 나중에 추가한다.
- **포함:** 리소스 테이블, 한정자 매칭, 참조·테마·스타일 해석, 인플레이터가 쓸 layout·drawable XML 트리
- **제외:** 뷰 생성과 그리기, 애니메이션 실행. 리소스 계층은 값만 돌려준다.
- **의존성:** XML 파서 pugixml 하나. 이미지 디코딩은 각 플랫폼 API에 맡긴다.

## 모듈 구성

![모듈 구성 · 어댑터 3개, 공유 코어, 빌드 도구](module-structure.svg)

GTK4와 WinUI 3 어댑터는 C++ 코어를 직접 부르고, Swift는 안정적인 C API 래퍼를 거친다. 개발 중에는 코어가 `res/`를 직접 읽고, 배포할 때는 `hydroid-rc`가 미리 만든 바이너리 테이블을 읽는다.

## 리소스 테이블 자료구조

테이블은 안드로이드 `resources.arsc`와 같은 구조다. 패키지 → 타입 → 항목 3단이고, 항목마다 한정자별 후보 값 목록을 갖는다. ID도 안드로이드와 같은 `0xPPTTEEEE` 형식을 써서 디버깅할 때 aapt2 출력과 바로 비교할 수 있게 한다.

- **패키지 ID:** 프레임워크(`android`)는 `0x01`, 앱은 `0x7f`. AppCompat 같은 라이브러리는 별도 패키지로 추가할 수 있게 둔다. 다른 패키지의 리소스는 `@패키지:타입/이름`으로 가리키고, 먼저 로드한 패키지에서 이름으로 찾는다(aapt2 link의 `-I`). 그 패키지가 `<public>`으로 연 것만 가리킬 수 있고, 나머지는 `@*` 같은 비공개 참조로만 가리킨다. 프레임워크 번들(6단계) 전에는 values의 `@android:` 참조가 "no package 'android' is loaded" 오류다(XML 파일의 `android:` 속성과 그 값 안의 @android: 참조는 건드리지 않는다).
- **타입·항목 ID:** aapt2와 같게 배정한다. `<public id="">`가 준 ID는 타입 ID까지 그대로 둔다. 나머지 타입은 남은 가장 낮은 타입 ID를 1부터 `ResType` 순서(anim, animator, array, attr, …)로 받는다. 있는 타입만 받고, 런타임에는 없는 styleable도 ID 하나를 차지한다. 나머지 항목은 타입 안에서 남은 가장 낮은 항목 ID를 0부터 이름의 바이트 순서로 받는다. 같은 입력이면 항상 같은 ID가 나온다.
- **이름 조회:** 인플레이터는 `@string/title`처럼 이름으로 찾으므로 `"패키지:타입/이름" → ResId` 해시맵을 둔다.

```cpp
using ResId = uint32_t;              // 0xPPTTEEEE

enum class ValueKind : uint8_t {
  Null, Empty,                       // @null, @empty
  Id,                                // id 리소스: <id name="..."/>, @+id/...
  Reference, AttrRef,                // @type/name, ?attr/name
  String, StyledString,              // <b> 등 서식 있는 문자열은 span 목록 포함
  Color, Dimension, Fraction, Float, Int, Bool,
  File,                              // layout/foo.xml, drawable/bg.png 같은 파일
  Style, Array, Plurals, AttrDecl, StyleableDecl
};

// 팩토리(Value::color, Value::string, ...)로만 만들고, 접근자가 종류를 확인한다
class Value {
 public:
  ValueKind kind() const;
  ResId    ref() const;              // Reference, AttrRef
  uint32_t argb() const;             // Color
  int32_t  intValue() const;         // Int
  uint32_t complexData() const;      // Dimension, Fraction (Res_value 형식)
  uint32_t index() const;            // 풀 위치: String, File, Style, Array, ...
  // ...
};

struct Candidate { Config config; Value value; };  // Config는 다음 절

struct Entry {
  uint16_t id;                       // 항목 ID
  std::string name;
  bool isPublic;                     // <public>
  std::vector<Candidate> candidates; // 설정마다 하나, compareConfigs 순서
};

struct TypeTable { ResType type; uint8_t id; std::vector<Entry> entries; };

struct Package {
  uint8_t id; std::string name;
  std::vector<TypeTable> types;
  // 값 종류마다 풀 하나. Value::index()가 그 안의 위치를 가리킨다
  std::vector<std::string> strings;
  std::vector<StyledString> styledStrings;
  std::vector<FileRef> files;
  std::vector<Style> styles;
  std::vector<Array> arrays;
  std::vector<Plurals> plurals;
  std::vector<AttrDecl> attrs;
  std::vector<StyleableDecl> styleables;
};
```

문자열과 복합값은 값 종류마다 `Package`의 풀에 두고 `Value::index()`로 가리킨다.

| 종류 | 저장 형태 |
| --- | --- |
| style | 부모 ResId + (attr ResId, Value) 목록. attr ID로 정렬해 이진 탐색 |
| array, string-array, integer-array | Value 목록 |
| plurals | zero·one·two·few·many·other 6칸 |
| attr 선언 | format 비트, 최솟값·최댓값, enum·flag 목록(이름의 id 리소스 ResId → 값, 이름 순서) |
| declare-styleable | attr ResId 목록. 인플레이터가 obtainStyledAttributes에 넘김 |
| color/ 상태 목록 | (상태 조건 목록, 색) 목록. 8단계에서 더한다. 지금은 files 풀의 FileRef(APK 안 경로와 파일 종류)뿐 |
| layout·drawable·xml 파일 | 파싱된 XML 트리. 속성값은 미리 Value로 바꿔 둠. 8단계에서 더한다. 지금은 files 풀의 FileRef(APK 안 경로와 파일 종류)뿐 |

문자열은 패키지의 strings 풀에 한 번만 저장한다. 배포용 바이너리 테이블(`.hrt`)은 이 구조를 포인터 대신 오프셋으로 직렬화해서, 파일을 mmap으로 한 번에 올려 바로 쓸 수 있게 한다.

## Configuration(한정자) 모델

`Config` 하나로 폴더 이름의 한정자와 현재 기기 상태를 모두 표현한다. 값은 안드로이드 `AConfiguration`(`ResTable_config`)의 값과 같고, 0이면 "지정 안 됨"(폴더에 없거나 기기가 모름)이다. 폴더 이름은 `-`로 자른 뒤 토큰마다 아래 표 순서로 맞춰 본다. 순서가 뒤바뀌었거나 모르는 토큰이 있으면 aapt2처럼 `invalid configuration` 오류로 로드가 실패한다. aapt2가 숫자를 잘라 다른 값으로 받는 이름(`sw65536dp` 등), 바이트가 깨지는 `b+` 로케일, 어느 형식이든 문자·숫자가 아닌 글자가 든 로케일(`en-r+x`)은 일부러 거부한다(`ConfigParser.h`). 표의 순서는 `Config` 필드의 순서이자 매칭의 우선순위다(isBetterThan만 두 숨김 상태를 입력 방식·내비게이션 방식보다 먼저 본다).

| 순서 | 한정자 | 예 |
| --- | --- | --- |
| 1 | MCC·MNC | `mcc450`, `mcc450-mnc05` |
| 2 | 언어·지역 | `ko`, `ko-rKR`, `b+sr+Latn`, `b+de+DE+1901` |
| 3 | 문법적 성별 | `feminine`, `masculine`, `neuter` |
| 4 | 레이아웃 방향 | `ldltr`, `ldrtl` |
| 5 | 최소 너비 | `sw600dp` |
| 6 | 가용 너비 | `w720dp` |
| 7 | 가용 높이 | `h480dp` |
| 8 | 화면 크기 | `small`, `normal`, `large`, `xlarge` |
| 9 | 화면 비율 | `long`, `notlong` |
| 10 | 둥근 화면 | `round`, `notround` |
| 11 | 넓은 색역 | `widecg`, `nowidecg` |
| 12 | HDR | `highdr`, `lowdr` |
| 13 | 방향 | `port`, `land`, `square` |
| 14 | UI 모드 | `car`, `desk`, `television`, `appliance`, `watch`, `vrheadset` |
| 15 | 야간 모드 | `night`, `notnight` |
| 16 | 밀도 | `ldpi`\~`xxxhdpi`, `tvdpi`, `nodpi`, `anydpi`, `400dpi` |
| 17 | 터치스크린 | `notouch`, `stylus`, `finger` |
| 18 | 키보드 상태 | `keysexposed`, `keyshidden`, `keyssoft` |
| 19 | 입력 방식 | `nokeys`, `qwerty`, `12key` |
| 20 | 내비게이션 키 상태 | `navexposed`, `navhidden` |
| 21 | 내비게이션 방식 | `nonav`, `dpad`, `trackball`, `wheel` |
| 22 | 화면 픽셀 크기 | `640x480` (Android 3.2부터 폐기됐지만 aapt2는 읽는다) |
| 23 | 플랫폼 버전 | `v21`, `v36.1` (항상 마지막) |

```cpp
// 필드 순서 = 폴더 이름의 한정자 순서 = 우선순위. 열거형은 안드로이드와 같은 값이고 Any = 0
struct Config {
  uint16_t mcc = 0, mnc = 0;                       // mnc0·mnc00·mnc000은 kMncZero
  std::array<char, 4> lang{}, region{}, script{};  // "ko", "KR", "Latn"
  std::array<char, 8> variant{};                   // "posix", "1901"
  GrammaticalGender grammaticalGender{};
  LayoutDir layoutDir{};
  uint16_t smallestWidthDp = 0, widthDp = 0, heightDp = 0;
  ScreenSize screenSize{};
  ScreenLong screenLong{};
  ScreenRound screenRound{};
  WideColorGamut wideColorGamut{};
  Hdr hdr{};
  Orientation orientation{};
  UiModeType uiModeType{};
  UiModeNight uiModeNight{};
  uint16_t density = 0;                            // 120 ldpi, 160 mdpi, 213 tvdpi, 240 hdpi,
                                                   // 320 xhdpi, 480 xxhdpi, 640 xxxhdpi,
                                                   // 0xFFFE anydpi, 0xFFFF nodpi
  Touchscreen touchscreen{};
  KeysHidden keysHidden{};
  Keyboard keyboard{};
  NavHidden navHidden{};
  Navigation navigation{};
  uint16_t screenWidth = 0, screenHeight = 0;      // 640x480
  uint16_t sdkVersion = 0, minorVersion = 0;       // v36.1
};

int compareConfigs(const Config& a, const Config& b);  // aapt2가 항목의 후보를 두는 순서
std::string toString(const Config& config);            // "en-rUS-sw600dp-v13"
bool matches(const Config& config, const Config& device);                   // 다음 절
bool isBetterThan(const Config& a, const Config& b, const Config& device);  // 다음 절
```

- **지역 표기 두 가지:** `ko-rKR`(지역 앞에 `r`)과 BCP-47 형식 `b+ko+KR`을 모두 받는다. variant는 `b+` 형식으로만 줄 수 있고(`b+de+DE+1901`), aapt2처럼 별개의 설정이 된다.
- **옛 언어 코드:** 안드로이드는 리소스를 옛 코드로 찾는다. 그래서 폴더 이름은 그대로 두고, 기기 로케일의 `he`·`id`·`yi`를 `iw`·`in`·`ji`로 바꾼다(`Resources::setConfiguration`). 히브리어는 `values-iw/`이고, `values-he/`는 어떤 기기에서도 선택되지 않는다(golden `locale_he-IL`).
- **기기 Config:** 안드로이드가 리소스에 넘기는 모양으로 둔다. 로케일의 스크립트는 태그가 지정했을 때만 두고(`sr-Latn`), 나머지는 매칭이 언어와 지역으로 알아낸다. 화면 픽셀(`screenWidth`×`screenHeight`)은 기기를 어떻게 들든 긴 쪽이 너비다. `navHidden`은 안드로이드가 리소스에 넘기지 않으므로 비워 두고, 그래서 `values-navexposed/`·`values-navhidden/`은 어떤 기기에서도 선택되지 않는다. 소프트 키보드를 쓰는 기기의 `keysHidden`은 `keyssoft`다. 언어, 버전, 화면 픽셀, `navHidden`은 `Resources::setConfiguration`이 맞춰 준다.
- **기기 sdkVersion:** 기기 Config에 버전이 없으면(`sdkVersion`과 `minorVersion`이 모두 0이면) Hydroid가 흉내 낼 API 레벨 **37**(Android 17)로 채운다(`Version.h`의 `kEmulatedSdkVersion`). 버전을 직접 준 기기(테스트 등)는 그대로 쓴다. 그래야 `-v21`, `-v26` 폴더가 최신 안드로이드처럼 선택된다. 비교 기준도 모두 이 레벨에 맞춘다: golden 덤프는 API 37.0 에뮬레이터, golden 앱은 compileSdk·targetSdk 37(AGP 9.5.1), aapt2 golden은 build-tools 37.0.0이 `platforms/android-37.0`에 link한 결과다. `@android:` 프레임워크 리소스도 번들할 때(6단계) android-37.0에서 가져온다. 지금은 `golden/aosp`에 android17-release의 폴더 목록만 있다. 매칭 코드(`ConfigMatch.cpp`, `LocaleData.cpp`)도 같은 릴리스에서 옮겼고, 로케일 표(`LocaleDataTables.inc`)는 `golden/aosp`와 같은 커밋에서 만든다(`scripts/gen_locale_data.py`의 BRANCH·COMMIT). 37은 이것들이 모두 나와 있는 가장 높은 레벨이다. 37.1이나 37.2가 아니라 37.0을 쓰는 것은 비교에 마이너 버전이 끼지 않게 하려는 것이다. 레벨을 올릴 때는 이들을 함께 올리고, 하나만 어긋나면 `SdkBaseline` 테스트가 실패한다(AGP 버전과 로케일 표의 내용은 검사하지 않는다).
- **암묵적 버전:** aapt2는 일부 한정자에 최소 버전을 자동으로 붙인다(`hdpi`는 `hdpi-v4`, `anydpi`는 `anydpi-v21`, `feminine`은 `feminine-v34`). `parseConfig`도 같게 붙이므로 aapt2 출력과 같은 Config가 나온다.
- **숫자 체계(#51):** 기기 로케일의 숫자 체계(`ar-EG-u-nu-latn`의 `nu-latn`)는 다루지 않는다. 폴더 이름으로는 줄 수 없어(aapt2 link가 `b+ar+u+nu+latn`을 거부) 리소스 후보는 모두 비어 있고, 안드로이드의 로케일 비교는 후보끼리 숫자 체계가 다를 때만 이 값을 쓴다.

## 한정자 매칭 알고리즘

안드로이드 런타임(android17-release의 `AssetManager2::FindEntry`와 `ResourceTypes.cpp`의 `ResTable_config::match`, `isBetterThan`)과 같은 2단계로 고른다. 기기와 모순되는 후보를 버리고, 남은 후보를 테이블 순서(`compareConfigs`)대로 보며 더 나은 것이 나오면 바꾼다. 항목당 후보가 몇 개뿐이라 선형 탐색으로 충분하다. 규칙은 API 37.0 에뮬레이터가 고른 값을 덤프한 golden(`GoldenMatchTest`)으로 확인한다.

```cpp
// Resources.h
const Candidate* bestCandidate(const Entry& e, const Config& device) {
  const Candidate* best = nullptr;
  for (const Candidate& c : e.candidates) {      // compareConfigs 순서
    if (!matches(c.config, device)) continue;
    if (!best || isBetterThan(c.config, best->config, device)) best = &c;
  }
  return best;   // nullptr → 맞는 값 없음
}

class Resources {                                // 테이블 하나와 기기 설정 하나
 public:
  explicit Resources(const ResourceTable& table);
  void setConfiguration(const Config& device, const std::vector<Config>& locales = {});
  Config configuration() const;                  // 언어를 고른 뒤의 기기 설정
  uint32_t generation() const;                   // 결과가 바뀔 수 있을 때마다 오른다
  const Candidate* candidate(ResId id, uint32_t* generation = nullptr) const;
};
```

**1단계 — match: 모순되는 후보 버리기.** 후보가 지정한 한정자가 하나라도 기기와 맞지 않으면 탈락이다. 지정하지 않은 한정자는 항상 통과한다.

- **같아야 통과:** MCC·MNC, 문법적 성별, 레이아웃 방향, 화면 비율, 둥근 화면, 넓은 색역, HDR, 방향, UI 모드, 야간 모드, 터치스크린, 키보드 상태, 입력 방식, 내비게이션 키 상태, 내비게이션 방식. 단 `keysexposed`는 `keyssoft` 기기에도 맞는다. 내비게이션 키 상태는 기기 쪽이 늘 비어 있어(앞 절의 "기기 Config") 지정한 후보가 늘 탈락한다.
- **기기 값 이하이면 통과:** 최소 너비, 가용 너비·높이, 화면 크기, 화면 픽셀 크기, 플랫폼 버전(부 버전 포함)
- **탈락시키지 않음:** 밀도. 어떤 밀도든 크기를 조정해 쓸 수 있으니 2단계에서만 비교한다.
- **로케일:** 언어가 같고(`tl`과 `fil`은 같은 언어), 스크립트가 같아야 한다. 스크립트는 로케일이 지정한 것, 없으면 그 언어를 그 지역에서 주로 쓰는 글자다. `zh-rTW`는 Hant라서 zh-HK 기기에 맞고 `zh-rCN`은 맞지 않는다. `en-rGB`는 en-AU 기기에 맞는다. 지역과 variant로는 거르지 않는다. 스크립트를 알 수 없는 언어(사적 사용 언어 등)에서만 후보가 지정한 지역이 기기와 같아야 한다. 안드로이드 7 이전에는 지역으로 걸렀다.

**2단계 — isBetterThan: 둘 중 나은 후보.** `Config` 필드 순서로(단 두 숨김 상태는 입력 방식·내비게이션 방식보다 먼저) 두 후보를 비교하다가 처음 차이 나는 한정자에서 결정한다. 단 로케일은 더 나은 쪽일 때만 거기서 결정하고, 더 나쁘면 다음 한정자로 넘어간다. 그래서 두 후보가 서로보다 나을 수 있고(한국어 태블릿에서 `ko`와 `sw600dp`), 결과는 후보를 보는 순서에 달려 있다. `bestCandidate`는 안드로이드처럼 후보를 `compareConfigs` 순서로 본다.

- 대부분: 한쪽만 지정했으면 지정한 쪽이 이긴다. 기기가 그 한정자 값을 모르면(0) 건너뛴다. 키보드 상태는 둘 다 지정했을 수 있는데(`keyssoft` 기기의 `keysexposed`와 `keyssoft`), 그때는 기기와 같은 쪽이 이긴다.
- 최소 너비·버전: 큰 값. 가용 너비·높이와 화면 픽셀 크기: 둘을 함께 봐서 기기에 더 가까운 쪽.
- 화면 크기: 큰 값. 기기 화면이 normal 이상이면 미지정을 normal로 본다.
- 로케일: 언어를 지정한 쪽. 단 en-US 기기와 그에 가까운 영어 기기에서는 언어 미지정이 다른 영어(`en-rGB`)보다 낫다. 그다음 기기 로케일의 부모 트리에서 지역이 더 가까운 쪽(es-MX 기기에 `es-r419`), 그다음 variant가 같은 쪽이다.
- 밀도는 아래 특례를 따른다.

**밀도 특례.** 밀도를 지정하지 않은 후보는 mdpi(160)로 보고, 기기 밀도가 없거나 `anydpi`여도 160으로 본다. `anydpi` 후보는 어떤 밀도보다 낫다. 그 밖에는 두 후보 중 높은 쪽이 기기 밀도와 같으면 높은 쪽, 둘 다 기기보다 높으면 기기에 가까운 낮은 쪽, 나머지(하나는 낮고 하나는 높거나, 둘 다 낮을 때)는 높은 쪽이다. 미지정과 `mdpi`처럼 같은 밀도로 보는 두 후보는 나중에 본 쪽(아래 `a`)을 높은 쪽으로 친다. 그래서 기기가 160 이상이면 나중에 본 쪽이, 160보다 낮으면 먼저 본 쪽이 이긴다. android17-release의 코드를 그대로 옮겼다.

```cpp
// a, b: 두 후보의 밀도(미지정은 160), req: 기기 밀도(없거나 anydpi면 160). true면 a가 이긴다
bool densityBetter(int a, int b, int req) {
  if (a == kDensityAny) return true;  // anydpi는 크기를 조정하지 않아도 된다
  if (b == kDensityAny) return false;
  int h = std::max(a, b), l = std::min(a, b);
  bool aBigger = a >= b;              // 같으면(미지정과 mdpi) a를 높은 쪽으로 본다
  if (h == req) return aBigger;       // 높은 쪽이 기기와 같음
  if (l >= req) return !aBigger;      // 둘 다 기기보다 높음 → 낮은 쪽
  return aBigger;                     // 그 밖에는 높은 쪽(줄여서 쓴다)
}
```

예를 들어 기기가 xxhdpi(480)이고 후보가 xhdpi(320)와 xxxhdpi(640)뿐이면 640을 고른다. 크게 만든 이미지를 줄이는 쪽이 더 선명하기 때문이다.

**언어 선택.** 기기의 선호 언어 목록(예: ko-KR, en-US)을 앞에서부터 보며, 테이블(`android` 패키지 제외)에 그 언어와 스크립트의 리소스가 있는 첫 언어를 기기 언어로 정한 뒤 매칭한다(`Resources::setConfiguration`). `android` 밖의 패키지에 언어를 지정한 값이 없으면(의사 로케일 en-XA·ar-XB뿐이어도) `android` 패키지까지 보고 고른다. 영어(의사 로케일 en-XA 제외)는 리소스가 없어도 있는 것으로 친다. 안드로이드는 언어 미지정 리소스를 영어로 보기 때문이다. 안드로이드처럼 테이블의 `tl`은 `fil`로 바꿔 비교하고 기기의 `tl`은 그대로 두므로, `values-tl/`은 tl-PH를 고르는 근거가 되지 않는다(골라진 뒤의 매칭에서는 둘이 같은 언어다). 맞는 언어가 하나도 없거나 목록에 하나뿐이면 첫 언어다. 고르는 기준은 리소스별이 아니라 테이블 전체라서, 고른 언어의 값이 없는 리소스는 다음 언어가 아니라 언어 미지정 값(`values/`)을 쓴다. 부모 로케일 대체(es-MX → es-419 → es)는 위의 로케일 매칭이 맡는다. Android 15+에서 앱이 `LocaleConfig`로 기본 로케일을 선언했으면(`Resources::setLocaleConfig`) 이 규칙이 달라진다. 목록에서 앱이 지원하는 언어를 모두 순서대로 읽고, 리소스마다 그중 값이 있는 첫 언어의 값을 쓴다. 그래서 첫 언어의 값이 없는 리소스는 다음 언어의 값을 쓴다. 언어 미지정 값(`values/`)은 기본 로케일의 값으로 본다. 안드로이드처럼 `LocaleConfig`는 다음 `setConfiguration`부터 적용된다(golden `localeconfig`).

**캐시.** `Resources`가 `ResId`마다 결과를 캐시하고, 세대 번호(`generation()`)가 바뀌면 버린다. 세대 번호는 `setConfiguration`이 기기 Config를 바꾸거나(회전·다크 모드·배율 변경) 테이블이 바뀔 때 오른다. 결과에서 만든 값도 이 번호로 캐시할 수 있다.

## 값 해석: 참조·테마·스타일

후보를 고른 다음에는 참조를 끝까지 따라가고, 테마·스타일 규칙에 따라 속성값을 정한다. 이 부분이 안드로이드 `obtainStyledAttributes`와 같아야 뷰의 크기와 색이 안드로이드와 일치한다.

**참조 따라가기**

- `@type/name`: 테이블에서 다시 resolve한다. 참조가 또 참조를 가리킬 수 있으니 반복하되, 깊이 제한과 방문 집합으로 순환을 잡는다.
- `@android:type/name`: 프레임워크 패키지(0x01)에서 찾는다.
- `?attr/name`, `?android:attr/name`: 현재 Theme에서 찾고, 나온 값이 또 참조면 위 규칙을 반복한다.
- `@null`은 빈 값, `@+id/name`은 로드 때 id 타입 항목으로 등록한다.

**프레임워크 리소스 번들.** 앱 리소스 대부분이 `@android:`와 `?android:attr/`에 기대므로, AOSP `frameworks/base/core/res/res`의 android-37.0 버전에서 필요한 부분(attrs.xml, themes, styles, colors, dimens)을 Hydroid에 같이 넣는다. AOSP는 Apache 2.0이라 번들할 수 있다. AppCompat·Material Components의 `app:` 속성과 테마는 별도 패키지로 나중에 추가한다.

**스타일 상속**

- `parent="@style/X"`가 있으면 그것이 부모다.
- 없으면 점 이름 규칙을 쓴다. `Theme.App.Dark`의 부모는 `Theme.App`이며, 같은 패키지 안에서만 적용한다.
- `parent=""`이면 부모가 없다.
- 부모부터 자식 순으로 덮어써서 `attr → Value` 평탄화 맵을 만들고 캐시한다.

**Theme.** 적용한 스타일들(`applyStyle`, force 여부)을 순서대로 합친 평탄화 맵이다. 기기 Config가 바뀌면 다시 만든다.

**obtainStyledAttributes 우선순위.** 요청된 속성마다 위에서부터 처음 찾은 값을 쓴다.

1. 레이아웃 XML에 직접 쓴 속성
2. 그 요소의 `style="@style/X"`
3. defStyleAttr: 테마에서 그 속성(예: `?android:attr/textViewStyle`)이 가리키는 스타일
4. defStyleRes: 3번이 0이거나 테마에 없을 때만
5. 테마에 직접 들어 있는 같은 속성

찾은 값이 `?attr`이면 테마로, `@`이면 테이블로 해석한 뒤 TypedArray에 담는다. `android:textAppearance`처럼 뷰가 따로 처리하는 속성은 직접 속성보다 낮은 우선순위로 뷰 쪽에서 구현한다.

```cpp
class Theme {
public:
  void applyStyle(ResId style, bool force);
  std::optional<Value> attr(ResId attr) const;
};

TypedArray obtainStyledAttributes(const Theme& theme,
                                  const XmlElement* element,      // 없으면 nullptr
                                  const std::vector<ResId>& attrs, // declare-styleable 목록
                                  ResId defStyleAttr, ResId defStyleRes);
```

## 개발 스텝

저장소를 둘로 나눈다. `hydroid-res`는 UI 프레임워크에 전혀 의존하지 않는 C++ 라이브러리이고, `hydroid-view`(가칭)가 플랫폼별 뷰와 어댑터를 가진다. 경계는 "값과 트리까지는 res, 객체 생성과 그리기는 view"다.

### 경계: 누가 무엇을 맡나

| 기능 | hydroid-res | hydroid-view |
| --- | --- | --- |
| 기기 Config | Config 구조체와 매칭 | 플랫폼에서 값 수집, 변경 감지 |
| 이미지 | 밀도 버킷 선택, 파일 경로·바이트 반환 | 디코딩과 그리기 |
| drawable XML | shape·selector·vector 등을 트리로 파싱 | 트리를 해석해 실제로 그림 |
| layout XML | 파싱된 트리, obtainStyledAttributes | 뷰 객체 생성(LayoutInflater) |
| 폰트 | font-family XML 해석, 파일 경로 | 폰트 로드와 텍스트 측정 |
| 테마·스타일 | Theme, TypedArray | TypedArray 값을 뷰 속성에 적용 |

`hydroid-res`는 UIKit·GTK·WinRT 헤더를 하나도 include하지 않는다. 그래서 화면 없이 CI에서 전부 테스트할 수 있다.

### 두 프로젝트를 잇는 방법

- **API 먼저:** R1에서 공개 C 헤더 초안을 확정한다. view는 그 헤더만 보고 개발한다.
- **버전 고정:** res가 릴리스 태그(v0.1, v0.2 …)를 내면 view는 그 버전을 의존성으로 고정한다. iOS는 XCFramework를 담은 Swift Package, GTK4·WinUI 3는 CMake 패키지로 받는다.
- **목 값:** res의 해당 버전이 나오기 전에는 view에서 하드코딩한 값으로 먼저 개발한다.

### hydroid-res 단계

| 단계 | 내용 | 완료 기준 | 릴리스 |
| --- | --- | --- | --- |
| R1 | 골격(CMake, pugixml, GoogleTest, 3개 OS CI), 공개 C API 초안, 골든 덤프 도구 | 세 OS에서 CI 통과 |  |
| R2 | 기본 값 파서(color, dimen, integer, bool, fraction) | 색 4형식·단위 6종 테스트 통과 |  |
| R3 | Config, 폴더 이름 파서 | AOSP 프레임워크 res/ 폴더 전부 파싱 |  |
| R4 | 스캐너, values 파서, ResourceTable | `aapt2 dump resources`와 일치 |  |
| R5 | 한정자 매칭, 언어 선택, 결과 캐시 | 조건 조합 골든 일치 | v0.1 · 값 조회 |
| R6 | 참조 해석, 프레임워크 리소스 번들 | `@android:` 포함 골든 일치 |  |
| R7 | 스타일·테마·obtainStyledAttributes | TextView·Button 속성 덤프 일치 | v0.2 · 스타일 |
| R8 | 상태별 색 목록, plurals, layout·drawable·font XML 트리 | 트리 덤프가 원본 XML과 일치 | v0.3 · 트리 |
| R9 | `hydroid-rc` 빌드 도구, `.hrt` 로더 | `.hrt` 결과가 res/ 직접 로드와 동일 | v0.4 · 바이너리 |
| R10 | API 고정, 패키징(XCFramework·CMake), 문서 | view가 바이너리 의존성만으로 빌드됨 | v1.0 |

### hydroid-view 단계

| 단계 | 내용 | 필요한 res | 완료 기준 |
| --- | --- | --- | --- |
| V1 | iOS 뷰 보강: RTL, baseline 정렬, ConstraintLayout 등 README의 남은 항목 | 없음(목 값) | 코드로 만든 샘플이 안드로이드와 같은 배치 |
| V2 | iOS 어댑터: Config 수집·변경 알림, Swift 래퍼 | v0.1 | 회전·다크 모드 때 값이 다시 해석됨 |
| V3 | 비트맵·나인패치 drawable | v0.1 | 밀도별 이미지 선택·늘이기가 안드로이드와 같음 |
| V4 | LayoutInflater(iOS) | v0.3 | 샘플 레이아웃 XML이 안드로이드와 같은 좌표로 배치됨 |
| V5 | XML drawable(shape, selector, layer-list, vector), 상태별 색 반영 | v0.3 | 스크린샷 비교 일치 |
| V6 | GTK4 뷰 계층과 어댑터 | v0.3 | V4·V5 샘플이 같은 결과 |
| V7 | WinUI 3 뷰 계층과 어댑터 | v0.3 | V4·V5 샘플이 같은 결과 |
| V8 | 배포 빌드를 `.hrt` 로딩으로 전환 | v0.4 | 세 플랫폼 시작 시간 측정 |

R1\~R5와 V1은 동시에 진행할 수 있다. 그 뒤로는 view가 res 릴리스를 따라간다. GTK4와 WinUI 3는 둘 다 C++이므로, V6 전에 measure/layout 로직을 두 플랫폼이 공유하는 C++ 뷰 코어로 묶어 두면 V7이 훨씬 가벼진다.

V2·V6·V7의 어댑터가 채울 기기 값과 출처는 다음과 같다.

| 기기 값 | UIKit | GTK4 | WinUI 3 |
| --- | --- | --- | --- |
| 밀도(배율) | `traitCollection.displayScale` | `gdk_surface_get_scale` | `XamlRoot.RasterizationScale` |
| 야간 모드 | `userInterfaceStyle` | `AdwStyleManager` 또는 `GtkSettings` | `ActualTheme` |
| 언어 목록 | `Locale.preferredLanguages` | `g_get_language_names` | `GetUserPreferredUILanguages` |
| 화면 크기(dp) | 창 bounds(pt) | 창 크기(논리 픽셀) | `XamlRoot.Size` |
| 레이아웃 방향 | `effectiveUserInterfaceLayoutDirection` | `gtk_widget_get_direction` | `FlowDirection` |
| 이미지 디코딩 | `UIImage` | `GdkTexture` | WIC / `BitmapImage` |

## 테스트 전략

정답은 언제나 실제 안드로이드다. 같은 `res/`와 같은 조건에서 안드로이드가 고른 값을 골든 데이터로 기록해 두고 Hydroid 결과와 비교한다.

- **골든 데이터:** 테스트용 안드로이드 앱에 한정자 폴더를 많이 만든다. 계측 테스트에서 `createConfigurationContext`로 조건을 바꿔 가며 모든 리소스의 해석 결과를 JSON으로 덤프하고, Hydroid 결과와 항목별로 비교한다.
- **조건 조합:** 밀도 7종, 언어(ko, en, 리소스가 없는 언어), 야간 on/off, 가로·세로, 최소 너비(sw320dp, sw600dp, sw720dp)를 조합한다.
- **스타일 해석:** TextView·Button을 여러 테마에서 만들고 TypedArray 값을 덤프해 비교한다.
- **테이블 검증:** `aapt2 dump resources` 출력과 항목 수·값을 비교한다.
- **파서 퍼징:** 폴더 이름과 values XML을 무작위로 만들어 크래시가 없는지 확인한다.
- **성능 측정:** 테이블 로드 시간, resolve 1회 비용, 메모리를 플랫폼별로 기록한다. 목표치는 첫 측정 후 정한다.
- **CI:** 세 플랫폼 모두 같은 골든 테스트를 돌린다.
