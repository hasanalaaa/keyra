import { useState } from 'preact/hooks';
import { Contour, drawOnce } from '../components/Icon';
import { Button, Rich } from '../components/ui';
import { t, type Key } from '../lib/i18n';
import { go } from '../lib/router';
import { LangButton } from './common';

const STEPS: Key[] = ['welcomeStep1', 'welcomeStep2', 'welcomeStep3'];

export function Welcome() {
  const [draw] = useState(drawOnce);
  return (
    <div class="page hero-page paper">
      <div class="top-actions">
        <LangButton />
      </div>
      <div class="hero-col">
        <div class="hero-card welcome">
          <div class="lockup" role="img" aria-label="Keyra">
            <Contour size={112} draw={draw} class="hero-mark" />
            <span class="wordmark" aria-hidden="true">
              Keyra
            </span>
          </div>
          <h1 class="display hero-title">{t('welcomeTitle')}</h1>
          <p class="subtitle">{t('welcomeSub')}</p>
          <ol class="steps">
            {STEPS.map((key, i) => (
              <li key={key} class="step-row">
                <span class="step-n mono" aria-hidden="true">
                  {`0${i + 1}`}
                </span>
                <span class="step-text">
                  <Rich text={t(key)} />
                </span>
              </li>
            ))}
          </ol>
          <div class="hero-foot">
            <Button size="lg" full onClick={() => go('/setup/1')}>
              {t('welcomeCta')}
            </Button>
            <p class="caption center">{t('welcomeCaption')}</p>
          </div>
        </div>
      </div>
    </div>
  );
}
