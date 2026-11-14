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
import java.util.Locale

/**
 * Dumps how Android resolves every app resource under a matrix of device conditions.
 * One JSON file per condition lands in files/golden/; scripts/golden/pull_golden.py copies them
 * to golden/data/, where the C++ golden tests compare hydroid-res against them.
 */
@RunWith(AndroidJUnit4::class)
class GoldenDumpTest {

    data class Condition(
        val densityDpi: Int,
        val language: String,
        val night: Boolean,
        val landscape: Boolean,
        val smallestWidthDp: Int,
    ) {
        val name: String
            get() = listOf(
                "${densityDpi}dpi", language, if (night) "night" else "notnight",
                if (landscape) "land" else "port", "sw${smallestWidthDp}dp",
            ).joinToString("_")
    }

    private val densities = listOf(120, 160, 213, 240, 320, 480, 640)
    private val languages = listOf("ko-KR", "en-US", "ja-JP")  // ja has no resources
    private val smallestWidths = listOf(320, 600, 720)

    // Types the dump covers; later steps add style, array, plurals, layout and drawable.
    private val types = listOf("string", "color", "dimen", "integer", "bool")

    @Test
    fun dumpAll() {
        val target = InstrumentationRegistry.getInstrumentation().targetContext
        val outDir = File(target.filesDir, "golden").apply {
            deleteRecursively()
            mkdirs()
        }
        for (d in densities) for (l in languages) for (n in listOf(false, true))
            for (land in listOf(false, true)) for (sw in smallestWidths) {
                val c = Condition(d, l, n, land, sw)
                File(outDir, "${c.name}.json").writeText(dump(target, c).toString(2))
            }
    }

    private fun configFor(base: Context, c: Condition): Configuration =
        Configuration(base.resources.configuration).apply {
            densityDpi = c.densityDpi
            setLocales(LocaleList(Locale.forLanguageTag(c.language)))
            uiMode = (uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or
                if (c.night) Configuration.UI_MODE_NIGHT_YES else Configuration.UI_MODE_NIGHT_NO
            orientation =
                if (c.landscape) Configuration.ORIENTATION_LANDSCAPE else Configuration.ORIENTATION_PORTRAIT
            smallestScreenWidthDp = c.smallestWidthDp
            val longSide = c.smallestWidthDp * 16 / 9
            screenWidthDp = if (c.landscape) longSide else c.smallestWidthDp
            screenHeightDp = if (c.landscape) c.smallestWidthDp else longSide
        }

    private fun dump(base: Context, c: Condition): JSONObject {
        val config = configFor(base, c)
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
                })
            }
        }
        return JSONObject().apply {
            put("condition", JSONObject().apply {
                put("densityDpi", c.densityDpi)
                put("locale", c.language)
                put("night", c.night)
                put("orientation", if (c.landscape) "land" else "port")
                put("smallestWidthDp", c.smallestWidthDp)
                put("screenWidthDp", config.screenWidthDp)
                put("screenHeightDp", config.screenHeightDp)
                put("sdkInt", android.os.Build.VERSION.SDK_INT)
            })
            put("values", values)
        }
    }
}
