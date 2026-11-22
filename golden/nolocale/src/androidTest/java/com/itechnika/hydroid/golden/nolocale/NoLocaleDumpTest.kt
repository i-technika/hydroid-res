package com.itechnika.hydroid.golden.nolocale

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import android.os.Build
import android.os.LocaleList
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * Dumps the locale Android picks of a list for an app with no locales but the pseudo-locales
 * (#252). ResourcesImpl.updateConfiguration then picks of the locales the system's resources have,
 * AssetManager.getLocales, where an app with a locale of its own picks of its own,
 * getNonSystemLocales. The lifecycles are files as LocaleConfigDumpTest writes, which
 * tests/GoldenLifecycle.h replays: scripts/golden/pull_golden.py --app nolocale copies them to
 * golden/nolocale/data/, which GoldenNoLocaleTest reads.
 */
@RunWith(AndroidJUnit4::class)
class NoLocaleDumpTest {

    /** A step of a lifecycle: the locales of the device. */
    class Step(val name: String, val locales: String, val landscape: Boolean = false)

    class Lifecycle(val name: String, val steps: List<Step>)

    // Welsh (cy) and Irish (ga) are languages the system has no resources of (system_locales.txt),
    // and Japanese and Korean ones it has.
    private val lifecycles = listOf(
        // ja, which only the system has, after one neither has.
        Lifecycle("cy+ja", listOf(
            Step("created", "cy-GB,ja-JP"),
            Step("rotated", "cy-GB,ja-JP", landscape = true),
            Step("changed", "ga-IE,ko-KR,ja-JP"),
        )),
        // The first is one the system has.
        Lifecycle("ja+ko", listOf(Step("created", "ja-JP,ko-KR"))),
        // None the system has: the first.
        Lifecycle("cy+ga", listOf(Step("created", "cy-GB,ga-IE"))),
        // English, after one neither has.
        Lifecycle("cy+en", listOf(Step("created", "cy-GB,en-US"))),
        // Pseudo-locales, which the system has too: the app's of them count with the system's,
        // where they are no locale of the app's, which this does not tell apart.
        Lifecycle("cy+en-XA+ja", listOf(Step("created", "cy-GB,en-XA,ja-JP"))),
        Lifecycle("cy+ar-XB+ja", listOf(Step("created", "cy-GB,ar-XB,ja-JP"))),
        // Languages the system's resources have under another code: iw, and tl.
        Lifecycle("cy+he", listOf(Step("created", "cy-GB,he-IL"))),
        Lifecycle("cy+fil", listOf(Step("created", "cy-GB,fil-PH"))),
    )

    @Test
    fun dumpAll() {
        check(Build.VERSION.SDK_INT >= 36) { "needs API 36, which has SDK_INT_FULL" }
        val target = InstrumentationRegistry.getInstrumentation().targetContext
        val outDir = File(target.filesDir, "golden").apply {
            deleteRecursively()
            mkdirs()
        }
        lifecycles.forEachIndexed { i, lifecycle ->
            File(outDir, "${fileName(lifecycle.name)}.json").writeText(dump(target, lifecycle, i).toString(2))
        }
        // The locales the system's resources have, of framework-res.apk as the device has it,
        // which the golden tests' table has as a package android (tests/GoldenLifecycle.h).
        File(outDir, "system_locales.txt").writeText(
            Resources.getSystem().assets.locales.sorted().joinToString("\n", postfix = "\n"))
    }

    private fun fileName(name: String) = name.replace(' ', '_').replace(',', '+')

    private fun Configuration.set(step: Step, scale: Float) {
        densityDpi = 320
        setLocales(LocaleList.forLanguageTags(step.locales))
        uiMode = (uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or Configuration.UI_MODE_NIGHT_NO
        orientation =
            if (step.landscape) Configuration.ORIENTATION_LANDSCAPE else Configuration.ORIENTATION_PORTRAIT
        smallestScreenWidthDp = 320
        screenWidthDp = if (step.landscape) 568 else 320
        screenHeightDp = if (step.landscape) 320 else 568
        // Each lifecycle its own, so that ResourcesManager, which keeps a ResourcesImpl for each
        // configuration it is asked for, makes a new one for it.
        fontScale = scale
    }

    /** The steps of `lifecycle`: the first makes the Resources, the others update it. */
    private fun dump(base: Context, lifecycle: Lifecycle, index: Int): JSONObject {
        val scale = 1f + (index + 1) / 64f
        var res: Resources? = null
        val steps = JSONArray()
        for (step in lifecycle.steps) {
            val config = Configuration(base.resources.configuration).apply { set(step, scale) }
            val r = res
            if (r == null) {
                res = base.createConfigurationContext(config).resources
            } else {
                @Suppress("DEPRECATION")
                r.updateConfiguration(config, r.displayMetrics)
            }
            steps.put(step(step.name, step.locales, res!!))
        }
        // The app has no LocaleConfig.
        return JSONObject().put("name", lifecycle.name).put("localeConfig", false).put("steps", steps)
    }

    /** What `res` picks of every string, and the Configuration it picks for, as GoldenDumpTest. */
    private fun step(name: String, requested: String, res: Resources): JSONObject {
        val values = JSONObject()
        val rClass = Class.forName("${javaClass.`package`!!.name}.R\$string")
        for (field in rClass.fields.sortedBy { it.name }) {
            values.put("string/${field.name}", JSONObject().put("string", res.getString(field.getInt(null))))
        }
        val used = res.configuration
        return JSONObject().apply {
            put("step", name)
            put("condition", JSONObject().apply {
                put("requestedLocales", requested)
                put("sdkInt", Build.VERSION.SDK_INT)
                put("sdkIntFull", Build.VERSION.SDK_INT_FULL)
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
            put("values", values)
        }
    }
}
