package com.itechnika.hydroid.golden.localeconfig

import android.app.LocaleManager
import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import android.os.Build
import android.os.LocaleList
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * Dumps how Android picks each resource's language for an app with a LocaleConfig and a default
 * locale (res/xml/locales_config.xml), over the life of its Resources (#249): as they are made for
 * a list of locales, as the device turns with the same list, and as the list changes.
 *
 * Android gives a Resources its app's LocaleConfig only after it has been made, in
 * ContextImpl.setResources, and only if it has more than one locale then; the
 * ResourcesImpl made has already chosen its locales by then. So what each step picks can hang on
 * the steps before it, and a lifecycle is one file of steps. scripts/golden/pull_golden.py
 * --app localeconfig copies them to golden/localeconfig/data/, which GoldenLocaleConfigTest reads.
 */
@RunWith(AndroidJUnit4::class)
class LocaleConfigDumpTest {

    /** A step of a lifecycle: the locales of the device, and what else it changes. */
    class Step(
        val name: String,
        val locales: String,
        val landscape: Boolean = false,
        val mcc: Int = 0,
    )

    class Lifecycle(val name: String, val steps: List<Step>)

    private val lifecycles = listOf(
        // All supported: fr is chosen at first, for the whole table.
        Lifecycle("fr+es", listOf(
            Step("created", "fr-FR,es-ES"),
            Step("rotated", "fr-FR,es-ES", landscape = true),
            Step("changed", "es-ES,fr-FR"),
            Step("rotated again", "es-ES,fr-FR", landscape = true),
        )),
        // de, which the table has and the app does not support, first.
        Lifecycle("de+fr+es", listOf(
            Step("created", "de-DE,fr-FR,es-ES"),
            Step("rotated", "de-DE,fr-FR,es-ES", landscape = true),
            Step("changed to none supported", "it-IT,de-DE"),
            Step("rotated again", "it-IT,de-DE", landscape = true),
        )),
        // ko, which the table has not, first: fr is chosen, and put first.
        Lifecycle("ko+fr+es", listOf(
            Step("created", "ko-KR,fr-FR,es-ES"),
            Step("rotated", "ko-KR,fr-FR,es-ES", landscape = true),
            Step("changed to the reordered", "fr-FR,ko-KR,es-ES"),
            Step("rotated again", "fr-FR,ko-KR,es-ES", landscape = true),
        )),
        // The default locale between the others.
        Lifecycle("fr+en+es", listOf(
            Step("created", "fr-FR,en-US,es-ES"),
            Step("rotated", "fr-FR,en-US,es-ES", landscape = true),
        )),
        // None supported, one the table has.
        Lifecycle("ja+ko", listOf(
            Step("created", "ja-JP,ko-KR"),
            Step("rotated", "ja-JP,ko-KR", landscape = true),
        )),
        // None supported, none the table has.
        Lifecycle("ko+it", listOf(
            Step("created", "ko-KR,it-IT"),
            Step("rotated", "ko-KR,it-IT", landscape = true),
        )),
        // One locale at first, so no LocaleConfig is given, then more.
        Lifecycle("fr then fr+es", listOf(
            Step("created", "fr-FR"),
            Step("changed", "fr-FR,es-ES"),
            Step("rotated", "fr-FR,es-ES", landscape = true),
        )),
        // An mcc the device has, of Spanish.
        Lifecycle("mcc310 fr+es", listOf(
            Step("created", "fr-FR,es-ES", mcc = 310),
            Step("rotated", "fr-FR,es-ES", landscape = true, mcc = 310),
        )),
    )

    // The languages the app itself is set to, as a user sets them in Settings (LocaleManager).
    private val appLocales = listOf("fr-FR,de-DE", "de-DE,es-ES")

    @Test
    fun dumpAll() {
        check(Build.VERSION.SDK_INT >= 35) { "needs API 35, which has the default locale" }
        val target = InstrumentationRegistry.getInstrumentation().targetContext
        val outDir = File(target.filesDir, "golden").apply {
            deleteRecursively()
            mkdirs()
        }
        lifecycles.forEachIndexed { i, lifecycle ->
            File(outDir, "${fileName(lifecycle.name)}.json").writeText(dump(target, lifecycle, i).toString(2))
        }
        // The app's own Resources, made as the process started, of the device's one language, so
        // ContextImpl.setResources gave it no LocaleConfig; the app's languages then update it,
        // as the system gives them, before the device's.
        check(target.resources.configuration.locales.size() == 1) { "the device has more than one language" }
        val manager = target.getSystemService(LocaleManager::class.java)
        try {
            for (tags in appLocales) {
                setAppLocales(target, manager, LocaleList.forLanguageTags(tags))
                val given = target.resources.configuration.locales.toLanguageTags()
                val steps = JSONArray().put(step("app locales", given, target.resources))
                File(outDir, "${fileName("app $tags")}.json").writeText(
                    JSONObject().put("name", "app $tags").put("localeConfig", false)
                        .put("steps", steps).toString(2))
            }
        } finally {
            setAppLocales(target, manager, LocaleList.getEmptyLocaleList())
        }
    }

    private fun fileName(name: String) = name.replace(' ', '_').replace(',', '+')

    /** Sets the app's own languages, and waits until its resources have them first. */
    private fun setAppLocales(target: Context, manager: LocaleManager, locales: LocaleList) {
        manager.applicationLocales = locales
        val deadline = SystemClock.uptimeMillis() + 10_000
        while (!locales.isEmpty && target.resources.configuration.locales.get(0) != locales.get(0)) {
            check(SystemClock.uptimeMillis() < deadline) { "the app was not given $locales" }
            Thread.sleep(50)
        }
    }

    private fun Configuration.set(step: Step, scale: Float) {
        densityDpi = 320
        setLocales(LocaleList.forLanguageTags(step.locales))
        uiMode = (uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or Configuration.UI_MODE_NIGHT_NO
        orientation =
            if (step.landscape) Configuration.ORIENTATION_LANDSCAPE else Configuration.ORIENTATION_PORTRAIT
        smallestScreenWidthDp = 320
        screenWidthDp = if (step.landscape) 568 else 320
        screenHeightDp = if (step.landscape) 320 else 568
        mcc = step.mcc
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
        // ContextImpl.setResources gives the app's LocaleConfig to Resources made of more than one
        // language, after the first step.
        val given = LocaleList.forLanguageTags(lifecycle.steps.first().locales).size() > 1
        return JSONObject().put("name", lifecycle.name).put("localeConfig", given).put("steps", steps)
    }

    /** What `res` picks of every string, and the Configuration it picks for, as GoldenDumpTest. */
    private fun step(name: String, requested: String, res: Resources): JSONObject {
        val values = JSONObject()
        val rClass = Class.forName("${javaClass.`package`!!.name}.R\$string")
        for (field in rClass.fields.sortedBy { it.name }) {
            val id = field.getInt(null)
            // A resource that has no value for one of the locales read in has none at all.
            val text = try {
                res.getString(id)
            } catch (e: Resources.NotFoundException) {
                null
            }
            values.put("string/${field.name}", JSONObject().put("string", text ?: JSONObject.NULL))
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
