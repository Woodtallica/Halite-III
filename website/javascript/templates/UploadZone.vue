<template>
    <div class="panel panel-default upload-zone"
         @dragenter="drag_over = true"
         @dragover="drag_over = true"
         @dragleave="drag_over = false"
         v-bind:class="{ dragging: drag_over }">
        <div class="panel-body">
       <!-- <img class="upload-zone-icon" :src="icon" alt=""> -->
            <h2 v-if="title">{{ title }}</h2>
            <p v-if="description" v-html="description" class="upload-zone-desc"></p>
            <p class="hidden" v-if="message">{{ message }}</p>
            <input class="form-control" type="file" v-on:change="on_changed" />
            <p v-if="caption" class="upload-zone-caption">{{ caption }}</p>

            <div class="progress hidden" v-if="progressBar">
                <div class="progress-bar progress-bar-striped active" role="progressbar"
                     :aria-valuenow="progress" aria-valuemin="0" aria-valuemax="100"
                     :style="'width: ' + progress.toString() + '%;'">
                    {{ progress }}%
                </div>
            </div>
        </div>
    </div>
</template>

<script>
    export default {
      name: 'halite-upload-zone',
      props: ['title', 'message', 'icon', 'description', 'caption', 'progress', 'progressBar', 'buttonText'],
      data: function () {
        return {
          drag_over: false
        }
  },
      methods: {
        on_changed: function (event) {
          this.drag_over = false
          this.$emit('change',
            Array.prototype.slice.call(event.target.files))
          event.target.value = null
        }
      }
    }
</script>

<style lang="scss" scoped>
    .upload-zone {
        position: relative;

        h2 {
            text-align: center;
            font-weight: 300;
        }

        p {
            margin: 0px 0px;
        }

        input {
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            width: 100%;
            height: 100%;
            cursor: pointer;
            opacity: 0;
        }
    }
</style>
