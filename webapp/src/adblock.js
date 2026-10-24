import { configRead } from './config';
import './adblock.css';

function syncAdblockStyles() {
  document.documentElement.classList.toggle(
    'ytaf-adblock-enabled', Boolean(configRead('enableAdBlock'))
  );
}

export function userScriptStartAdBlock() {
  syncAdblockStyles();
}

document.addEventListener('ytaf-config-changed', (event) => {
  if (event.detail && event.detail.key === 'enableAdBlock') {
    syncAdblockStyles();
  }
}, true);
