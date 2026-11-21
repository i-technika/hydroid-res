package com.itechnika.hydroid.golden

import android.content.Context
import android.content.res.Configuration
import android.os.LocaleList
import android.util.TypedValue
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONObject
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * Dumps how Android resolves every app resource under a matrix of device conditions.
 * One JSON file per condition lands in files/golden/; scripts/golden/pull_golden.py copies them
 * to golden/data/, where the C++ golden tests compare hydroid-res against them.
 */
@RunWith(AndroidJUnit4::class)
class GoldenDumpTest {

    /** A device condition: the name of its file, and what it makes of the device's Configuration. */
    class Condition(val name: String, val change: Configuration.() -> Unit)

    private val densities = listOf(120, 160, 213, 240, 320, 480, 640)
    private val languages = listOf("ko-KR", "en-US", "ja-JP")  // ja has no resources
    private val smallestWidths = listOf(320, 600, 720)

    // Types the dump covers; later steps add style, array, plurals, layout and drawable.
    private val types = listOf("string", "color", "dimen", "fraction", "integer", "bool")

    // Bases for Resources.getFraction: "%" scales the first, "%p" the second.
    private val fractionBase = 360
    private val fractionParentBase = 1234

    @Test
    fun dumpAll() {
        val target = InstrumentationRegistry.getInstrumentation().targetContext
        val outDir = File(target.filesDir, "golden").apply {
            deleteRecursively()
            mkdirs()
        }
        val conditions = matrix() + extras()
        check(conditions.map { it.name }.toSet().size == conditions.size) { "a name is used twice" }
        for (c in conditions) {
            File(outDir, "${c.name}.json").writeText(dump(target, c).toString(2))
        }
    }

    /** What every condition sets; the matrix varies all of it, and an extra changes something else. */
    private fun Configuration.set(
        density: Int = 320,
        locales: String = "en-US",
        night: Boolean = false,
        landscape: Boolean = false,
        smallestWidthDp: Int = 320,
    ) {
        densityDpi = density
        setLocales(LocaleList.forLanguageTags(locales))
        uiMode = (uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or
            if (night) Configuration.UI_MODE_NIGHT_YES else Configuration.UI_MODE_NIGHT_NO
        orientation =
            if (landscape) Configuration.ORIENTATION_LANDSCAPE else Configuration.ORIENTATION_PORTRAIT
        smallestScreenWidthDp = smallestWidthDp
        val longSide = smallestWidthDp * 16 / 9
        screenWidthDp = if (landscape) longSide else smallestWidthDp
        screenHeightDp = if (landscape) smallestWidthDp else longSide
    }

    /** 7 densities x 3 languages x night x orientation x 3 smallest widths. */
    private fun matrix(): List<Condition> = buildList {
        for (d in densities) for (l in languages) for (n in listOf(false, true))
            for (land in listOf(false, true)) for (sw in smallestWidths) {
                val name = listOf(
                    "${d}dpi", l, if (n) "night" else "notnight",
                    if (land) "land" else "port", "sw${sw}dp",
                ).joinToString("_")
                add(Condition(name) { set(d, l, n, land, sw) })
            }
    }

    /**
     * One condition for each thing the matrix does not vary, on 320dpi_en-US_notnight_port_sw320dp:
     * the rules of matching that the pick_ strings of values/match.xml are there for.
     */
    private fun extras(): List<Condition> = buildList {
        fun extra(name: String, change: Configuration.() -> Unit) =
            add(Condition(name) { set(); change() })
        fun Configuration.layout(mask: Int, value: Int) {
            screenLayout = (screenLayout and mask.inv()) or value
        }

        // A locale against the regions and scripts the app has, and with a variant, which no
        // folder of the app has (README.md, Golden data).
        val locales = listOf(
            "en-GB", "en-AU", "en-IN", "en-CA", "en-PH", "en-001", "en-XA", "en",
            "es-ES", "es-MX", "es-AR", "es-US", "es-419", "es-GQ",
            "zh-CN", "zh-TW", "zh-HK", "zh-MO", "zh-SG", "zh-Hant-CN", "zh",
            "sr-RS", "sr-Latn-RS", "sr-ME", "pt-BR", "pt-PT", "pt-AO", "pt",
            "fr-FR", "fr-CA", "fr-BE", "fr-CH", "ar-EG", "ar-DZ", "ar-SA", "ar-XB",
            "de-DE", "de-AT", "de-CH", "de-DE-1901", "he-IL", "id-ID", "fil-PH", "tl-PH",
            "qaa-US", "qaa-GB", "ko-KP", "th-TH",
        )
        for (tag in locales) extra("locale_$tag") { setLocales(LocaleList.forLanguageTags(tag)) }

        // Several languages, of which Android reads the resources in one.
        val localeLists = listOf(
            "ja-JP,ko-KR", "ja-JP,th-TH", "th-TH,ja-JP,en-US,ko-KR", "ja-JP,en-GB,ko-KR",
            "ja-JP,es-AR,ko-KR", "ja-JP,zh-HK,ko-KR", "ja-JP,zh-SG,ko-KR", "ja-JP,sr-ME,ko-KR",
            "ja-JP,he-IL,ko-KR", "ja-JP,id-ID,ko-KR", "ja-JP,fil-PH,ko-KR", "ja-JP,tl-PH,ko-KR",
            "ja-JP,qaa-US,ko-KR", "ja-JP,qaa-GB,ko-KR", "ja-JP,en-XA,ko-KR", "ko-KR,en-US",
            "ja-JP,de-AT,fr-FR", "en-US,ko-KR",
        )
        for (tags in localeLists) {
            extra("locales_" + tags.replace(",", "+")) { setLocales(LocaleList.forLanguageTags(tags)) }
        }

        for ((name, dpi) in listOf("400dpi" to 400, "560dpi" to 560, "200dpi" to 200)) {
            extra("density_$name") { densityDpi = dpi }
        }
        for ((w, h) in listOf(
            600 to 400, 400 to 600, 500 to 500, 599 to 399, 1000 to 1000, 900 to 500, 320 to 480,
        )) {
            extra("size_w${w}dp_h${h}dp") {
                screenWidthDp = w
                screenHeightDp = h
                smallestScreenWidthDp = minOf(w, h)
            }
        }
        for (sw in listOf(319, 800, 1000)) extra("size_sw${sw}dp") { smallestScreenWidthDp = sw }

        for ((name, size) in listOf(
            "small" to Configuration.SCREENLAYOUT_SIZE_SMALL,
            "normal" to Configuration.SCREENLAYOUT_SIZE_NORMAL,
            "large" to Configuration.SCREENLAYOUT_SIZE_LARGE,
            "xlarge" to Configuration.SCREENLAYOUT_SIZE_XLARGE,
        )) extra("screen_$name") { layout(Configuration.SCREENLAYOUT_SIZE_MASK, size) }
        extra("screen_long") {
            layout(Configuration.SCREENLAYOUT_LONG_MASK, Configuration.SCREENLAYOUT_LONG_YES)
        }
        extra("screen_notlong") {
            layout(Configuration.SCREENLAYOUT_LONG_MASK, Configuration.SCREENLAYOUT_LONG_NO)
        }
        extra("screen_round") {
            layout(Configuration.SCREENLAYOUT_ROUND_MASK, Configuration.SCREENLAYOUT_ROUND_YES)
        }
        extra("screen_notround") {
            layout(Configuration.SCREENLAYOUT_ROUND_MASK, Configuration.SCREENLAYOUT_ROUND_NO)
        }
        extra("screen_ldrtl") {
            layout(Configuration.SCREENLAYOUT_LAYOUTDIR_MASK, Configuration.SCREENLAYOUT_LAYOUTDIR_RTL)
        }
        extra("screen_ldltr") {
            layout(Configuration.SCREENLAYOUT_LAYOUTDIR_MASK, Configuration.SCREENLAYOUT_LAYOUTDIR_LTR)
        }
        for ((name, gamut) in listOf(
            "widecg" to Configuration.COLOR_MODE_WIDE_COLOR_GAMUT_YES,
            "nowidecg" to Configuration.COLOR_MODE_WIDE_COLOR_GAMUT_NO,
        )) extra("color_$name") {
            colorMode = (colorMode and Configuration.COLOR_MODE_WIDE_COLOR_GAMUT_MASK.inv()) or gamut
        }
        for ((name, hdr) in listOf(
            "highdr" to Configuration.COLOR_MODE_HDR_YES,
            "lowdr" to Configuration.COLOR_MODE_HDR_NO,
        )) extra("color_$name") {
            colorMode = (colorMode and Configuration.COLOR_MODE_HDR_MASK.inv()) or hdr
        }

        for ((name, type) in listOf(
            "normal" to Configuration.UI_MODE_TYPE_NORMAL,
            "desk" to Configuration.UI_MODE_TYPE_DESK,
            "car" to Configuration.UI_MODE_TYPE_CAR,
            "television" to Configuration.UI_MODE_TYPE_TELEVISION,
            "appliance" to Configuration.UI_MODE_TYPE_APPLIANCE,
            "watch" to Configuration.UI_MODE_TYPE_WATCH,
            "vrheadset" to Configuration.UI_MODE_TYPE_VR_HEADSET,
        )) extra("uimode_$name") {
            uiMode = (uiMode and Configuration.UI_MODE_TYPE_MASK.inv()) or type
        }

        for ((name, touch) in listOf(
            "notouch" to Configuration.TOUCHSCREEN_NOTOUCH,
            "stylus" to 2,  // Configuration.TOUCHSCREEN_STYLUS, which the SDK no longer names
            "finger" to Configuration.TOUCHSCREEN_FINGER,
        )) extra("touch_$name") { touchscreen = touch }
        extra("keys_exposed") {
            keyboardHidden = Configuration.KEYBOARDHIDDEN_NO
            hardKeyboardHidden = Configuration.HARDKEYBOARDHIDDEN_NO
        }
        extra("keys_hidden") {
            keyboardHidden = Configuration.KEYBOARDHIDDEN_YES
            hardKeyboardHidden = Configuration.HARDKEYBOARDHIDDEN_YES
        }
        // The keyboard that shows is the soft one: keyssoft to the resources.
        extra("keys_soft") {
            keyboardHidden = Configuration.KEYBOARDHIDDEN_NO
            hardKeyboardHidden = Configuration.HARDKEYBOARDHIDDEN_YES
        }
        for ((name, kind) in listOf(
            "nokeys" to Configuration.KEYBOARD_NOKEYS,
            "qwerty" to Configuration.KEYBOARD_QWERTY,
            "12key" to Configuration.KEYBOARD_12KEY,
        )) extra("keyboard_$name") { keyboard = kind }
        extra("nav_exposed") { navigationHidden = Configuration.NAVIGATIONHIDDEN_NO }
        extra("nav_hidden") { navigationHidden = Configuration.NAVIGATIONHIDDEN_YES }
        for ((name, kind) in listOf(
            "nonav" to Configuration.NAVIGATION_NONAV,
            "dpad" to Configuration.NAVIGATION_DPAD,
            "trackball" to Configuration.NAVIGATION_TRACKBALL,
            "wheel" to Configuration.NAVIGATION_WHEEL,
        )) extra("navigation_$name") { navigation = kind }

        extra("mcc310") { mcc = 310 }
        extra("mcc310_mnc260") {
            mcc = 310
            mnc = 260
        }
        extra("mcc310_mnc4") {
            mcc = 310
            mnc = 4
        }
        extra("mcc440") { mcc = 440 }
        extra("mcc450") { mcc = 450 }
        // No condition sets the grammatical gender: the SDK only sets it for the whole app,
        // through GrammaticalInflectionManager, and Configuration.setGrammaticalGender is hidden.

        // A better locale against a better width (pick_locale_or_width).
        for (tag in listOf("en-GB", "en-IN", "en-AU")) extra("locale_${tag}_sw600dp") {
            set(locales = tag, smallestWidthDp = 600)
        }
    }

    private fun dump(base: Context, c: Condition): JSONObject {
        val config = Configuration(base.resources.configuration).apply(c.change)
        val res = base.createConfigurationContext(config).resources
        val values = JSONObject()
        for (type in types) {
            val rClass = runCatching { Class.forName("${base.packageName}.R\$$type") }.getOrNull() ?: continue
            for (field in rClass.fields.sortedBy { it.name }) {
                val id = field.getInt(null)
                val tv = TypedValue()
                res.getValue(id, tv, true)
                values.put("$type/${field.name}", JSONObject().apply {
                    put("id", "0x%08x".format(id))
                    put("type", tv.type)
                    put("data", tv.data)
                    put("string", tv.coerceToString()?.toString() ?: JSONObject.NULL)
                    if (type == "dimen") put("px", res.getDimension(id).toDouble())
                    if (type == "fraction") {
                        put("base", fractionBase)
                        put("parentBase", fractionParentBase)
                        put("fraction", res.getFraction(id, fractionBase, fractionParentBase).toDouble())
                    }
                })
            }
        }
        // What the resources were resolved for, which is not quite what was asked: of several
        // locales Android puts the one it reads the resources in first.
        val used = res.configuration
        return JSONObject().apply {
            put("condition", JSONObject().apply {
                put("densityDpi", used.densityDpi)
                put("locale", config.locales.get(0).toLanguageTag())
                put("night", (used.uiMode and Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES)
                put("orientation", if (used.orientation == Configuration.ORIENTATION_LANDSCAPE) "land" else "port")
                put("smallestWidthDp", used.smallestScreenWidthDp)
                put("screenWidthDp", used.screenWidthDp)
                put("screenHeightDp", used.screenHeightDp)
                put("sdkInt", android.os.Build.VERSION.SDK_INT)
                // major * 100000 + minor, so 37.0 and 37.1 differ; only there from API 36.
                if (android.os.Build.VERSION.SDK_INT >= 36) {
                    put("sdkIntFull", android.os.Build.VERSION.SDK_INT_FULL)
                }
                // The locales that were asked for, and the whole Configuration of the resources, as
                // android.content.res.Configuration holds it, with the size of the display that
                // ResourcesImpl gives the native side as the screen in pixels.
                put("requestedLocales", config.locales.toLanguageTags())
                put("configuration", JSONObject().apply {
                    put("locales", used.locales.toLanguageTags())
                    put("mcc", used.mcc)
                    put("mnc", used.mnc)
                    put("grammaticalGender", used.grammaticalGender)
                    put("screenLayout", used.screenLayout)
                    put("smallestScreenWidthDp", used.smallestScreenWidthDp)
                    put("screenWidthDp", used.screenWidthDp)
                    put("screenHeightDp", used.screenHeightDp)
                    put("colorMode", used.colorMode)
                    put("orientation", used.orientation)
                    put("uiMode", used.uiMode)
                    put("densityDpi", used.densityDpi)
                    put("touchscreen", used.touchscreen)
                    put("keyboardHidden", used.keyboardHidden)
                    put("hardKeyboardHidden", used.hardKeyboardHidden)
                    put("keyboard", used.keyboard)
                    put("navigationHidden", used.navigationHidden)
                    put("navigation", used.navigation)
                    put("widthPixels", res.displayMetrics.widthPixels)
                    put("heightPixels", res.displayMetrics.heightPixels)
                })
            })
            // What getDimension multiplied by. xdpi comes from the physical display, not densityDpi.
            put("metrics", JSONObject().apply {
                put("density", res.displayMetrics.density.toDouble())
                put("scaledDensity", res.displayMetrics.scaledDensity.toDouble())
                put("xdpi", res.displayMetrics.xdpi.toDouble())
                put("fontScale", res.configuration.fontScale.toDouble())
            })
            put("values", values)
        }
    }
}
