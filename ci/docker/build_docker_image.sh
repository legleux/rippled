#!/usr/bin/env bash

set +euox pipefail
# Command line variables
FORCE_TAG=false
NPROC=$(nproc --ignore 3)
mem_limit=${MEM_LIMIT:-32} # xrpld in Docker can be greedy
limit_memory=true
registry=${REGISTRY:-legleux}
image_name="xrpld"
image="${registry}/${image_name}"
dry_run=${DRY_RUN:-false}
test=${TEST:-false}


check_git(){
    unknown_tag="_unknown_"
    if ! command -v git >/dev/null 2>&1; then
        echo "git not found in PATH, setting branch=$unknown_tag" >&2
    fi

    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "not inside a git repository, setting branch=$unknown_tag" >&2
        : try for BuildInfo.cpp?
    fi
}

set_branch() {
    if [[ -z "$BRANCH" ]]; then
        git_branch=$(git rev-parse --abbrev-ref HEAD)
        tag=$(git tag --points-at HEAD)
    fi
}

# git_short=$(git rev-parse --short HEAD)
# git_hash=$(git rev-parse HEAD)

# echo -e "Building:\n  git_branch: $git_branch\n  git_short:  $git_short\n  git_hash:   $git_hash\n"
tags=()
labels=()
build_args=()
args=()
# TODO: Check if these inherited from base image
# labels+=("org.opencontainers.image.ref.name=ubuntu")
# labels+=("org.opencontainers.image.version=22.04")

commit_id=$(git rev-parse HEAD)
commit_short_id=${commit_id:0:6}
tag=$(git tag --points-at HEAD | head -n1)
branch=$(git symbolic-ref --quiet --short HEAD || true)

if [[ -n "$tag" ]]; then
    ref_type="tag"
    ref_name="$tag"
    # tag_branch="$branch"
elif [[ -n "$branch" ]]; then
    ref_type="branch"
    ref_name="$branch"
else
    ref_type="detached"
    ref_name="$(git rev-parse --short HEAD)"
fi

is_prerelease=false
is_release=false
is_develop=false

if [[ "$ref_name" =~ -(rc|b|hf)[0-9]+$ ]]; then
    is_prerelease=true
    component=unstable
elif [[ "$ref_name" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    is_release=true
    component=stable
    tags+=("${image}:latest")
    # TODO: check the case where a hotfix release comes out after the latest but has a lower number
    # e.g. 2.5.2 fixes a bug but obviously 2.5.2 be "latest" if 2.6.0 already released?
    # Don't want to depend on build order for "latest" tag to be applied.
    # Would 2.5 and 2.6 have common ancestors? Maybe maybe not?
elif [[ "$ref_name" == "develop" ]]; then
    is_develop=true
    component=develop
    # Build debug so git has in version string
fi

tags+=("${image}:$ref_name")
labels+=("ref=${ref_name}")

# assemble our tags
if [ $is_release == true ]; then
    tags+=("${image}:latest")
fi
if [[ -n $tag_branch ]]; then
    tags+=("${image}:${branch}")
fi
tags+=("${image}:$commit_short_id")
tags+=("${image}:$commit_id")

echo "Tags:"
# TODO: Remove when rippled deprecated...
for tag in "${tags[@]}"; do
    rtags+=(${tag//xrpld/rippled})
done

tags=(${tags[@]} ${rtags[@]})

if [ $test = true ]; then
    component="${component}-test"
    tags=(${tags[@]//\//\/test-})
fi

for tag in "${tags[@]}"; do
    args+=("--tag=$tag")
    echo "  ${tag}"
    if [ -n "${ADD_TAGS}" ];then
        for tag in "${ADD_TAGS[@]}"; do
            args+=("--tag=$tag")
        done
    fi
done

labels+=("commit_id=${commit_id}")
echo "Labels:"
for label in "${labels[@]}"; do
    args+=(--label="$label")
    echo "  ${label}"
done

dockerfile=${DOCKERFILE:-ci/docker/Dockerfile}

if [ $test = true ]; then
    dockerfile="${dockerfile}.test"
    # args+=("--file=${dockerfile}")
fi

if [ $limit_memory = true ]; then
    # Using same limit prevents using swap
    args+=("--memory=${mem_limit}g")
    args+=("--memory-swap=${mem_limit}g")
fi

# repo_bundle="repo.bundle"
# if [ -f ${repo_bundle} ]; then
#     rm $repo_bundle
# fi

if [ ${ref_type} == "tag" ]; then
    echo "tag: $ref_name"
    echo "branch: $branch"
    # exit 0
    # git bundle create repo.bundle refs/heads/develop refs/tags/v3.2.0
    commitish="refs/heads/${branch} refs/tags/${ref_name}"
elif [ ${ref_type} == "branch" ]; then
    echo "ref_name: $ref_name"
    # exit 0
    commitish=refs/heads/${ref_name}
else
    : or else WHAT?!
fi

echo "commitish: $commitish"
# bundle_cmd=(git bundle create ${repo_bundle} ${commitish})
# echo "${bundle_cmd[@]}"
# exit 0
# if [ $dry_run = true ]; then
#     echo "${bundle_cmd[@]}"
# else
#     echo "Generating ${repo_bundle}..."
#     "${bundle_cmd[@]}" &>/dev/null
#     echo "Generated $(du -sh ${repo_bundle}) of"
#     mapfile -t heads < <(git bundle list-heads  ${repo_bundle})
#     printf '  %s\n' "${heads[@]}"
# fi

build_args+=("REF=${branch}")
build_args+=("NPROC=${NPROC}")
echo "Build args:"
for ba in "${build_args[@]}"; do
    echo "  ${ba}"
    args+=(--build-arg="${ba}")
done

context="."
args=(${context} --file=${dockerfile} ${args[@]})

printf '%s\n' \
"ref_type: $ref_type" \
"ref_name: $ref_name" \
"**************************"  \
"branch: ${branch}"  \
"tag: ${tag}" \
"commit_id: ${commit_id}" \
"commit_short_id: ${commit_short_id}" \
"**************************"  \
"component: $component" \
"is_release: $is_release" \
"is_prerelease: $is_prerelease" \
"test: $test" \
"dry_run: $dry_run" \
"**************************"

# arm=false
# if $arm;then
#     docker_builder="mac-builder"
#     arch="arm64"
#     platform="linux/arm64"
# else
#     docker_builder="default"
#     arch="amd64"
#     platform="linux/amd64"
# fi

# push_tag="${image}:${commit_id}-${arch}"
## Old per-arch tagging isn't needed with new buildx
main_tag="${image}:${commit_id}"
echo "Will push tag ${main_tag}"

confirm() {
  read -r -p "${1:-Are you sure?} [y/N] " reply
  [[ $reply =~ ^[Yy]([Ee][Ss])?$ ]]
}

if confirm "Ready to build [${main_tag}]?"; then
    echo "Let's go!"
else
    echo "Aborting build!"
    exit 0
fi

# docker buildx build \
#     --file=${dockerfile} \
#     --tag "${image}:${commit_id}-${arch}" \
#     --builder macbuilder \
#     --platform linux/arm64 --push ${context}
docker buildx build \
    --file=${dockerfile} \
    --progress=auto \
    --platform linux/arm64 \
    --tag "${main_tag}" \
    --load \
    ${context}


if confirm "Push ${main_tag} to Dockerhub?"; then
    docker push "${main_tag}"
else
    echo "Skipping ${main_tag} push"
fi
# docker buildx build \
#     --file=${dockerfile} \
#     --builder "${docker_builder}" \
#     --tag "${push_tag}" \
#     --platform "${platform}" --push ${context}
exit 0
cmd=(docker build ${args[@]})

if [ $dry_run = true ]; then
    echo "${cmd[@]}"
else
    "${cmd[@]}"
fi

exit 0

tags=$(docker inspect ${image}:$git_hash | jq -r ".[0].RepoTags.[]")
git_hash_label=$(docker inspect ${image}:$git_hash | jq -r ".[0].Config.Labels.git_hash")
git_branch_label=$(docker inspect ${image}:$git_hash | jq -r ".[0].Config.Labels.git_branch")
image_size=$(docker inspect ${image}:$git_hash | jq -r ".[0].Size" | numfmt --to=si)

printf "Repo tags:\n%s\nLabeled git hash:\n%s\nLabeled git branch:\n%s\nImage size:\n%s\n" $tags $git_hash_label $git_branch_label $image_size

confirm() {
  read -r -p "${1:-Are you sure?} [y/N] " reply
  [[ $reply =~ ^[Yy]([Ee][Ss])?$ ]]
}

# for tag in "${tags[@]}"; do
#     if confirm "Push ${tag} to Dockerhub?"; then
#       docker push "${tag}"
#     else
#       echo "Skipping ${tag} push"
#     fi
# done
