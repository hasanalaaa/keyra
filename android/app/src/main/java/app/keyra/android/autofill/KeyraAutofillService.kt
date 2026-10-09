package app.keyra.android.autofill

import android.app.ActivityOptions
import android.app.PendingIntent
import android.content.Intent
import android.content.IntentSender
import android.os.Build
import android.os.Bundle
import android.os.CancellationSignal
import android.service.autofill.AutofillService
import android.service.autofill.Dataset
import android.service.autofill.FillCallback
import android.service.autofill.FillRequest
import android.service.autofill.FillResponse
import android.service.autofill.SaveCallback
import android.service.autofill.SaveInfo
import android.service.autofill.SaveRequest
import android.util.Log
import android.view.autofill.AutofillId
import android.widget.RemoteViews
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.Entry
import app.keyra.android.api.KeyraApi
import app.keyra.android.match.HostMatcher
import app.keyra.android.store.appStore
import java.io.IOException
import java.security.GeneralSecurityException
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Mode A, button-gated typing: a suggestion never carries a value. Choosing one opens
 * [AutofillAuthActivity], which only asks Keyra to arm; Keyra then types into the field
 * as a keyboard after its button is pressed. Saving goes to [SaveActivity] (a press saves).
 */
class KeyraAutofillService : AutofillService() {

    override fun onFillRequest(request: FillRequest, cancellationSignal: CancellationSignal, callback: FillCallback) {
        val parsed = StructureParser.parse(request.fillContexts.last().structure, withValues = false)
        if (parsed.target.packageName == packageName || parsed.form.empty) return callback.onSuccess(null)
        val store = appStore(this)
        val api: KeyraApi = try {
            Keyra.api(this)
        } catch (e: GeneralSecurityException) {
            Log.w(TAG, "stored token unreadable", e)
            null
        } ?: return callback.onSuccess(null)
        val pinned = listOfNotNull(store.pinnedFor(parsed.target.key))
        val cancelled = AtomicBoolean(false)
        cancellationSignal.setOnCancelListener { cancelled.set(true) }
        Keyra.io.execute {
            // Unreachable or locked Keyra: still offer "Choose a login" and Save.
            val entries = try {
                api.entries()
            } catch (e: IOException) {
                Log.i(TAG, "logins unavailable: ${e.message}")
                emptyList()
            }
            if (!cancelled.get()) callback.onSuccess(response(parsed, HostMatcher.match(entries, parsed.target, pinned)))
        }
    }

    private fun response(parsed: ParsedStructure, matches: List<Entry>): FillResponse {
        val form = parsed.form
        val ids = parsed.idsOf(form.usernames + form.passwords)
        val what = form.what()
        val b = FillResponse.Builder()
        matches.forEachIndexed { i, e ->
            b.addDataset(dataset(ids, getString(R.string.af_dataset, e.title), sender(AutofillAuthActivity.armIntent(this, e, what), i)))
        }
        b.addDataset(dataset(ids, getString(R.string.af_pick), sender(AutofillAuthActivity.pickIntent(this, parsed.target, what), matches.size)))
        if (form.passwords.isNotEmpty()) {
            val type = SaveInfo.SAVE_DATA_TYPE_PASSWORD or (if (form.usernames.isEmpty()) 0 else SaveInfo.SAVE_DATA_TYPE_USERNAME)
            val save = SaveInfo.Builder(type, parsed.idsOf(form.passwords).toTypedArray())
            if (form.usernames.isNotEmpty()) save.setOptionalIds(parsed.idsOf(form.usernames).toTypedArray())
            b.setSaveInfo(save.build())
        }
        return b.build()
    }

    /** A suggestion with no values: the fields are only named so the system knows where it applies. */
    @Suppress("DEPRECATION") // Dataset.Builder(RemoteViews)/setValue: the replacement needs API 33.
    private fun dataset(ids: List<AutofillId>, label: String, auth: IntentSender): Dataset {
        val view = RemoteViews(packageName, R.layout.autofill_item).apply { setTextViewText(R.id.text, label) }
        val d = Dataset.Builder(view)
        for (id in ids) d.setValue(id, null)
        d.setAuthentication(auth)
        return d.build()
    }

    private fun sender(intent: Intent, requestCode: Int): IntentSender =
        PendingIntent.getActivity(this, requestCode, intent, PendingIntent.FLAG_CANCEL_CURRENT or PendingIntent.FLAG_IMMUTABLE, launchOptions()).intentSender

    /**
     * The form's app starts these screens, often while it is closing (a submitted login);
     * Android 14+ treats that as a background start, which the creator must allow.
     * TODO: the Android 16 emulator still sometimes drops the save screen when this app's
     * process was frozen before the save request; retire this note once checked on phones.
     */
    private fun launchOptions(): Bundle? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) return null
        val mode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.BAKLAVA) {
            ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOW_ALWAYS
        } else {
            @Suppress("DEPRECATION")
            ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED
        }
        return ActivityOptions.makeBasic().setPendingIntentCreatorBackgroundActivityStartMode(mode).toBundle()
    }

    override fun onSaveRequest(request: SaveRequest, callback: SaveCallback) {
        // The username may have been on an earlier screen of the same flow.
        val screens = request.fillContexts.map { StructureParser.parse(it.structure, withValues = true) }
        val last = screens.last()
        val password = last.textOf(last.form.passwords)
        if (password.isEmpty()) {
            Log.i(TAG, "save request without a password (${last.fields.size} fields, ${last.form.passwords.size} password fields)")
            return callback.onSuccess()
        }
        val username = screens.asReversed().map { it.textOf(it.form.usernames) }.firstOrNull { it.isNotEmpty() } ?: ""
        val intent = SaveActivity.intent(this, last.target, username, password)
        // Held only in this intent (RAM) until the confirmation screen sends it to Keyra.
        callback.onSuccess(PendingIntent.getActivity(this, SAVE_REQUEST, intent, PendingIntent.FLAG_CANCEL_CURRENT or PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_ONE_SHOT, launchOptions()).intentSender)
    }

    private companion object {
        const val TAG = "KeyraAutofill"
        const val SAVE_REQUEST = 1000
    }
}
