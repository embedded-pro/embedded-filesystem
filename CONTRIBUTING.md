# Contribution Guidelines

First off, thank you for taking the time to contribute!

Following these guidelines helps to communicate that you respect the time of the developers managing and developing this open source project. In return, they should reciprocate that respect in addressing your issue, assessing changes, and helping you finalize your pull requests.

## Contributing to development

embedded-filesystem is a young open source project and we love to receive contributions from our community — you! There are many ways to contribute, from writing examples, improving the documentation, submitting bug reports and feature requests or writing code which can be incorporated into embedded-filesystem itself.

## Ground Rules

* Be professional, respectful and considerate in your communication with others.
* Develop test first: every change comes with tests, and every performance or wear claim with a metric, as described in [docs/Testing.md](docs/Testing.md) and [docs/Metrics.md](docs/Metrics.md).
* Don't use the heap. This excludes most components from the standard template library, for which [embedded-infra-lib] provides heap-less alternatives.
* Never block: every operation that touches a storage device completes through the event dispatcher.
* Ensure cross-platform compatibility for your changes.
* Follow the rules in [AGENTS.md](AGENTS.md); when in doubt, look at the surrounding code that you are changing and stick to the local style.
* Create issues for any major changes and enhancements that you wish to make. Discuss things transparently and get feedback before you start.

## Your First Contribution

### Introduction

Submitting your first contribution (or Pull Request) can be scary, but we promise you it will be very rewarding! Information on how to proceed can be found on numerous blog posts and websites. A small selection of beginner friendly tutorials:

**Working on your first Pull Request?** You can learn how from this *free* series [How to Contribute to an Open Source Project on GitHub](https://app.egghead.io/playlists/how-to-contribute-to-an-open-source-project-on-github)

**Contributing to open source for the first time can be scary and a little overwhelming.** Perhaps you’re a [Code Newbie](https://www.codenewbie.org/) or maybe you’ve been coding for a while but haven’t found a project you felt comfortable contributing to. [You can do it! here's how.](https://www.firsttimersonly.com/)

#### Creating Pull Requests

1. Push your commit to get it back up to your fork: git push origin HEAD
2. Visit GitHub, click the “Pull request” button that it will make upon noticing your new branch.
3. In the description field, write down the issue number (if submitting code fixing an existing issue) or describe the issue and your fix (if submitting a wholly new bugfix).
4. Hit ‘submit’! And please be patient - the maintainers will get to you when they can.

Pull request titles follow [Conventional Commits](https://www.conventionalcommits.org/), starting with a lowercase subject, for instance `feat: add the fat directory iterator`.

## How to report a bug

If you find a security vulnerability, do NOT open an issue. Report it privately as described in [SECURITY.md](SECURITY.md).

In order to determine whether you are dealing with a security issue, ask yourself these two questions:

* Can I access something that's not mine, or something I shouldn't have access to?
* Can I disable something for other people?

If the answer to either of those two questions are "yes", then you're probably dealing with a security issue. Note that even if you answer "no" to both questions, you may still be dealing with a security issue, so if you're unsure, just report it privately.

When filing an issue, make sure to answer these five questions:

1. What compiler and compiler version are you using?
2. What operating system and processor architecture are you using?
3. What did you do?
4. What did you expect to see?
5. What did you see instead?

For problems with stored data, also mention the storage device, the filesystem and its configuration, and whether a power loss was involved.

[embedded-infra-lib]: https://github.com/embedded-pro/embedded-infra-lib
