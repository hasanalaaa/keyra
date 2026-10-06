import { Icon, LogoTile, type IconName } from '../components/Icon';
import { Button, Rich } from '../components/ui';
import { t, type Key } from '../lib/i18n';
import { go } from '../lib/router';
import { LangButton } from './common';

const STEPS: [IconName, Key][] = [
  ['usb', 'welcomeStep1'],
  ['user', 'welcomeStep2'],
  ['key-round', 'welcomeStep3'],
];

export function Welcome() {
  return (
    <div class="page glow-page">
      <div class="top-actions">
        <LangButton />
      </div>
      <div class="hero-col">
        <div class="hero-card welcome">
          <LogoTile size={88} />
          <h1 class="display">{t('welcomeTitle')}</h1>
          <p class="subtitle">{t('welcomeSub')}</p>
          <ol class="card steps">
            {STEPS.map(([icon, key]) => (
              <li key={key} class="step-row">
                <span class="step-icon">
                  <Icon name={icon} size={22} />
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
