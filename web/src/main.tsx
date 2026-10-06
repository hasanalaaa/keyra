import { render } from 'preact';
import './styles/tokens.css';
import './styles/base.css';
import './styles/components.css';
import './styles/screens.css';
import { App } from './App';
import { startApp } from './lib/store';
import { loadFont } from './lib/font';

loadFont();
startApp();
render(<App />, document.getElementById('app')!);
