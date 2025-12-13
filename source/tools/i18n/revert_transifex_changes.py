#!/usr/bin/env python3
import argparse
import configparser
import logging
import os
import sys
from collections.abc import Generator
from datetime import datetime
from pathlib import Path
from typing import Any
from urllib.parse import urljoin

import requests
from babel import Locale
from babel.localtime import LOCALTZ
from babel.messages import Catalog
from babel.messages.pofile import read_po, write_po
from requests import Session


logging.basicConfig(format="%(levelname)s: %(message)s", level=logging.INFO)
logger = logging.getLogger(__name__)


def get_tx_token() -> str:
    """Get credentials for Transifex.

    Credentials are either fetched from the TX_TOKEN environment
    variable or if that's not set from ~/.transifexrc.
    """
    token = os.environ.get("TX_TOKEN")
    if not token:
        config = configparser.ConfigParser()
        config.read(os.path.expanduser("~/.transifexrc"))
        token = config[config.sections()[0]]["token"]
    if not token:
        logger.error("Couldn't find auth token for Transifex. Aborting.")
        sys.exit(1)
    return token


def language_is_in_transifex(session: Session, args: argparse.Namespace) -> bool:
    """Check if a language is supported by the project in Transifex."""
    response = session.get(urljoin(args.api_url, f"projects/{args.project_id}/languages"))
    response.raise_for_status()

    data = response.json()
    for lang in data["data"]:  # noqa: SIM110
        if lang["attributes"]["code"] == args.language:
            return True
    return False


def get_resources(session: Session, args: argparse.Namespace) -> Generator[tuple[str, str]]:
    """Get different categories of strings from Transifex.

    For each resource we'll have a corresponding POT-file later on.
    """
    response = session.get(urljoin(args.api_url, f"resources?filter[project]={args.project_id}"))
    response.raise_for_status()

    while True:
        data = response.json()

        for resource in data["data"]:
            yield resource["attributes"]["slug"], resource["id"]

        if "next" not in data["links"] or not data["links"]["next"]:
            break

        response = session.get(data["links"]["next"])
        response.raise_for_status()


def get_strings_to_revert(
    session: Session, args: argparse.Namespace, resource: str
) -> Generator[dict[str, Any]]:
    """Pull strings to revert from Transifex.

    This pulls all strings for the given resource which are supposed to
    be reverted from Transifex.
    """
    response = session.get(
        urljoin(
            args.api_url,
            f"resource_translations?filter[resource]={resource}&"
            f"filter[language]=l:{args.language}&filter[translator]=u:{args.user}",
        )
    )
    response.raise_for_status()

    while True:
        data = response.json()

        for translated_resource_string in data["data"]:
            untranslated_resource_string_link = translated_resource_string["relationships"][
                "resource_string"
            ]["links"]["related"]

            response_untranslated_string = session.get(untranslated_resource_string_link)
            response_untranslated_string.raise_for_status()
            untranslated_resource_string = response_untranslated_string.json()["data"]

            yield untranslated_resource_string["attributes"]

        if "next" not in data["links"] or not data["links"]["next"]:
            break

        response = session.get(data["links"]["next"])
        response.raise_for_status()


def generate_pot_files(session: Session, args: argparse.Namespace):
    """Generate POT-files with the reverted translations.

    These translations can then be uploaded manually to Transifex.
    """
    locale = Locale.parse(args.language)

    for resource_name, resource_transifex_id in get_resources(session, args):
        logger.info("Processing %s", resource_name)

        input_pot_path = Path(args.input_pot_prefix) / f"{locale.language}.{resource_name}.po"
        try:
            with input_pot_path.open() as input_fd:
                catalog = read_po(input_fd, locale)
        except FileNotFoundError:
            logger.warning("Couldn't find %s. Skipping resource %s", input_pot_path, resource_name)
            continue

        new_catalog = Catalog(locale=locale)
        new_catalog.header_comment = catalog.header_comment
        new_catalog.mime_headers = catalog.mime_headers
        new_catalog.creation_date = datetime.now(LOCALTZ)

        empty_translations = []
        newer_source_strings = []

        for string_attributes in get_strings_to_revert(session, args, resource_transifex_id):
            source_strings = string_attributes["strings"]
            string_context = (
                string_attributes["context"] if string_attributes.get("context") else None
            )
            # one form only
            if len(source_strings) == 1:
                new_msgid = source_strings["other"]
            # singular + plural(s)  # noqa: ERA001
            else:
                new_msgid = tuple(source_strings.values())

            msg = catalog.get(new_msgid, context=string_context)
            if msg:
                if msg.string[0] == "":
                    empty_translations.append(new_msgid)
                    continue

                new_catalog.add(
                    new_msgid,
                    msg.string,
                    msg.locations,
                    msg.flags,
                    msg.auto_comments,
                    msg.user_comments,
                    lineno=msg.lineno,
                    context=msg.context,
                )
            else:
                newer_source_strings.append(new_msgid)

        if len(new_catalog) > 0:
            with (Path(args.output_pot_prefix) / f"{locale.language}.{resource_name}.po").open(
                "wb"
            ) as output_fd:
                write_po(output_fd, new_catalog)

        if empty_translations:
            logger.warning(
                "The following source string have an empty translation in the POT-file."
                " That's something Transifex doesn't support. For strings with "
                "plurarls this leads to an error during import, while for strings with "
                "a single form it's a no-op. These strings can't be reverted "
                "automatically and need to either be reverted manually or restored "
                "from a another POT-file."
            )
            for source_string in empty_translations:
                logger.warning(source_string)

        if newer_source_strings:
            logger.warning(
                "The following source strings were found in Transifex, but not in the POT-file."
                "This is probably because the source string got updated in 0ad after the POT-file "
                "got written. These strings can't be reverted automatically and need to either be "
                "reverted manually or restored from a more recent POT-file."
            )
            for source_string in newer_source_strings:
                logger.warning(source_string)


def main():
    parser = argparse.ArgumentParser(
        description="Revert all changes made by a user on Transifex.\n\n"
        "Searches Transifex for all strings to revert, extracts their supposed \n"
        "translation from existing POT-files and generates new POT-files with just \n"
        "the strings to revert, which can then be uploaded manually to Transifex.\n\n"
        "One limitation of this script is that it can't undo translations of strings\n"
        "which weren't translated before, as Transifex doesn't support that.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--api-url",
        default="https://rest.api.transifex.com/",
        help="URL of the Transifex API",
    )
    parser.add_argument(
        "--project-id",
        default="o:wildfire-games:p:0ad",
        help="Transifex Project Id",
    )
    parser.add_argument(
        "--language",
        help="Name of the language to revert changes for.",
        required=True,
    )
    parser.add_argument(
        "--user",
        help="Name of the user whose changes to revert.",
        required=True,
    )
    parser.add_argument(
        "--input-pot-prefix",
        help="Directory which contains the POT-files to read correct translations from",
        required=True,
    )
    parser.add_argument(
        "--output-pot-prefix",
        help="Directory to write the generated POT-files with all strings "
        "which need to be replaced to",
        required=True,
    )

    args = parser.parse_args()

    session = requests.session()
    session.headers.update(
        {"Authorization": f"Bearer {get_tx_token()}", "accept": "application/vnd.api+json"}
    )

    if not language_is_in_transifex(session, args):
        logger.error("%s isn't supported for this project on Transifex", args.language)
        sys.exit(1)

    generate_pot_files(session, args)


if __name__ == "__main__":
    main()
